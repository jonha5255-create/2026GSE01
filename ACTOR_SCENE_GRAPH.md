# Actor / SceneGraph 구조

## 책임 분리

- Actor: ID, 이름, 부모 ID, 로컬 위치, 활성/표시 상태, 갱신·렌더링 가상 함수.
- SceneGraph: 액터 단독 소유, 부모-자식 관계, 위치 전파, 생성·조회·지연 삭제, 갱신 및 렌더 순서.
- PlayerActor: 입력에 따른 이동 요청, 성장·체력·회복·피격 무적.
- WeaponActor: 자동 조준, 발사 간격, 발사체 생성. 플레이어의 자식이며 별도로 비활성화 가능.
- EnemyActor: 추적·접촉 공격·보스 패턴·피격. 보스 공격 예고는 자식 TelegraphActor.
- ProjectileActor / LootActor / EffectActor: 이동·충돌·흡인·획득·수명.
- Navigation: 가시성 및 BFS 경로장. LevelOne과 액터에서 재사용.
- LevelOne: 초기 플레이어 배치, 적 스폰, 파밍 목표, 보스 전환, 처치 보상 및 안내 대사. 개별 액터의 이동/충돌/수명 루프는 포함하지 않음.
- PrototypeWorld: 입력 전달, 64비트 청크 좌표, 스트리밍/충돌 서비스, 카메라와 ActorRenderContext 구현.
- PrototypeRenderer: 도형 배치, GPU 메시 캐시, 외부 셰이더와 HDR 후처리. 게임 규칙이나 액터 소유권은 없음.

## 실제 씬 구성

```text
SceneGraph
├─ PlayerActor
│  ├─ WeaponActor
│  └─ RangeActor
├─ ChunkActor × 25
│  ├─ FloorActor
│  ├─ BuildingActor × 4
│  └─ LampActor
├─ EnemyActor / Boss
│  └─ TelegraphActor (공격 예고 중에만 존재)
├─ ProjectileActor / LootActor / EffectActor
├─ PortalActor
├─ RainActor
└─ HudActor
```

화면에 배치되는 게임 오브젝트는 씬 그래프를 통해 렌더링합니다. 빗방울은 RainActor의 배치 효과이고 HUD의 패널·문자는 HudActor의 내부 도형입니다. 정점·삼각형·글자 하나씩을 액터로 만드는 구조는 아닙니다.

청크의 생성용 메타데이터는 별도로 유지하지만 실제 화면 배치와 상주 건물 충돌은 액터를 참조합니다. 건물 액터의 enabled를 끄면 갱신·표시·충돌이 제외됩니다. visible만 끄면 충돌은 유지됩니다. 청크 밖 건물의 충돌은 아직 로드되지 않은 절차 생성 데이터로 질의합니다.

## 위치와 수명주기 계약

- 현재 게임에 맞춘 2D 이동 계층입니다. local은 부모 기준 XY 위치이고 Position()은 부모 위치를 합산한 씬 위치입니다. 회전·비균일 스케일 행렬 계층은 이번 범위에 포함하지 않습니다.
- SetPosition()은 원하는 씬 위치에서 부모 위치를 빼 로컬 위치를 계산합니다.
- Reparent(id, parent, true)는 씬 위치를 보존합니다. false는 로컬 위치를 보존합니다. 자기 자신·자손으로의 연결 및 없는 부모는 거부합니다.
- 무한 월드는 청크 절대 좌표를 64비트 정수로 유지합니다. 플레이어 이동 시 씬의 월드 루트만 원점 보정하고 플레이어를 (0,0)으로 유지합니다. 자식은 자동으로 한 번만 따라가므로 보스 공격 예고가 플레이어를 추적하지 않습니다.
- UI·날씨 같은 screenSpace 루트는 월드 원점 보정 대상에서 제외됩니다.
- 부모의 enabled=false는 자식의 갱신과 표시까지 차단합니다. visible=false는 자식 표시만 차단합니다.
- Destroy()는 즉시 조회/갱신/표시 대상에서 제외하고 실제 메모리 해제는 순회가 끝난 뒤 수행합니다. 자식도 함께 삭제합니다.
- 순회는 시작 시점의 ID 목록을 사용합니다. 순회 중 생성된 액터는 다음 갱신부터 참여하며, 생성 이후 렌더 순회에서는 바로 표시할 수 있습니다.
- 액터는 그래프의 unique_ptr가 소유하며 복사할 수 없습니다. 외부에서 delete하지 않습니다. 포인터는 현재 순회 안에서만 임시 사용하고 장기 참조는 ActorId로 보관합니다. ID는 같은 SceneGraph 안에서 재사용하지 않으며 다른 씬/재시작 사이에는 유효하지 않습니다.
- GameplayContext는 한 틱의 서비스 묶음입니다. 충돌 함수는 값으로 소유하지만 SceneGraph·플레이어·진행 상태 참조는 해당 레벨 수명 안에서만 유효합니다.
- Player/Weapon/Portal은 레벨의 필수 액터이므로 레벨 진행 중 직접 제거하는 대신 비활성화하고, 전체 교체는 레벨 재시작으로 처리합니다.
- 건물은 생성된 청크 안의 정적 배치를 전제로 합니다. 청크 경계를 넘는 동적 장애물을 추가할 때는 별도의 광역 충돌 인덱스가 필요합니다.

## 실행 순서

Effect → Player(이동·상태) → Weapon(조준·발사) → Loot → Enemy → Projectile 순으로 갱신합니다. 같은 갱신 우선순위는 생성 순서입니다.

렌더링은 Ground → Telegraph → World → Projectile → Effect → Weather 순이며 같은 레이어는 x+y+depthBias로 정렬합니다. 동률이면 생성 순서를 유지합니다. HDR 합성 이후 UI 레이어를 별도로 순회합니다. 부모-자식 관계와 2.5D 가림 순서를 동시에 유지하기 위해 단순 DFS 그리기 대신 레이어별로 정렬합니다.

## 확장 예

```cpp
auto& enemy = level.Graph().Spawn<EnemyActor>(NoActor);
enemy.SetPosition({240, 80});
enemy.health = enemy.maximumHealth = 100;
ActorId handle = enemy.Id();

if (auto* actor = level.Graph().Get(handle))
{
    actor->enabled = false;
}

level.Graph().Destroy(handle);
// 순회 외부에서 즉시 정리하려는 경우에만 호출합니다.
level.Graph().CollectDestroyed();
```

새 동작은 Actor::Update를 재정의하고 필요한 서비스를 갱신 컨텍스트로 받습니다. 새 외형은 Render와 ActorRenderContext의 타입별 렌더 함수를 추가합니다. 기존 적의 스탯만 다른 경우 EnemyActor를 그대로 배치하면 됩니다. 다음 레벨은 동일 액터를 재사용하고 스폰·목표·보상 정책만 새로 작성할 수 있습니다.

## 검증

- SceneGraphTests.cpp: 위치 전파, 부모 변경, 순환 방지, 상위 활성/표시, 생성·삭제 중 순회, 깊이 정렬, 원점 보정.
- GameplayTests.cpp: 기존 조준·사거리·경험치·드랍·보스 테스트와 무기 활성/비활성, 보스 자식 예고 및 제거.
- PrototypeWorld::SelfTest: 청크 하위 액터 정리, 상주 수 제한, 건물 활성과 충돌, 액터 이동 중 청크 스트리밍, 재시작, 랜덤 맵 연결성.
- --smoke-test: 위 검사에 실제 OpenGL, 메시 캐시, HDR, UI, 화면 크기 변경 및 캡처를 포함합니다.
