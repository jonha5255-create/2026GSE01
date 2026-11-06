# 요괴·무기 모델과 공격 VFX 적용

2026-11-06 · SimpleGame / OpenGL 3.3

| 선택 | 무기 | 왼쪽 클릭: 일반 공격 | 오른쪽 클릭: 스킬 (요괴 Lv.3) |
|---|---|---|---|
| 1 | 묵의 검 | 푸른 참격 | 넓은 원형 참격 |
| 2 | 영혼총 | 총구 섬광과 탄도 | 부채꼴 영혼탄 |
| 3 | 쌍클로 | 보라색 발톱 궤적 | 삼연 발톱 |

숫자 키로 선택하면 동행 상태가 되며, `Q`로 무기화한다. 요괴 레벨 1·3·6·9에 동행과 무기 외형이 함께 진화한다. 이번에는 기존 1·2·3번만 연결했다.

[동행 외형 4단계](validation/yokai-vfx/yokai-companions.png) · [무기 외형 4단계](validation/yokai-vfx/yokai-weapons.png) · [일반 공격 3종과 스킬 3종](validation/yokai-vfx/attack-effects.png)

## 모델과 효과 연결

- `Art/LowPolyKit/data/yokai_weapon_catalog.json`의 노드와 소켓 정보를 사용한다. 요괴별 각 형태의 E03 GLB 하나를 공유하고 진화 단계에 따라 파츠를 표시한다. 검과 총은 손 애니메이션을 따르고, 총의 왼손은 보조 손잡이에 맞춘다. 클로는 한쪽 원본을 반전해 양손에 부착한다.
- `Art/LowPolyKit/data/weapon_vfx_catalog.json`의 GLB 애니메이션을 공격 시점에 재생한다. 검과 클로의 효과는 타격 동작에 맞추고, 삼연 발톱은 첫 타격 뒤 0.14초 간격으로 이어진다. 영혼총 섬광은 무기의 총구에 배치하고 탄도는 조준 방향으로 뻗는다. 적중 효과는 피해가 발생한 위치에 따로 표시한다.
- `WeaponVfxActor`가 SceneGraph에서 재생 시간을 관리한다. 무기화를 풀거나 요괴를 교체하면 이전 무기에서 재생 중인 공격 효과를 종료한다. GLB 지오메트리는 GPU에 캐싱하고, 매 프레임에는 노드 변환과 표시 상태를 갱신한다. 발광은 HDR 장면에 합성해 기존 블룸을 사용한다.

## 검증

Release x64에서 `--asset-test` 107개, `--smoke-test --seed=2026 --no-monitor` 239개 통과. `--yokai-art-test`는 24개 외형 조합과 6종 공격 렌더, 단계 전환 시 GPU 메시 재업로드 여부를 검사한다.

`--profile-test --profile-yokai --profile-frames=600 --seed=2026 --no-monitor`로 폐허에서 영혼총 자동 사격을 측정했다. AMD Radeon(TM) Graphics, VSync 해제, 60 FPS 제한 조건이다.

| 프레임 범위 | 평균 FPS | 평균 전체 드로 콜 | VFX 최대 드로 콜 |
|---|---:|---:|---:|
| 전체 600프레임 | 58.94 | 146.50 | 2 |
| 첫 60프레임 제외 | 59.72 | 146.36 | 2 |
| 첫 60프레임 이후 VFX 표시 78프레임 | 59.91 | 147.42 | 2 |

VFX가 표시된 워밍업 이후 78프레임의 GPU 모델 업로드는 0회였다. 전체 측정에서는 다른 모델이 처음 화면에 들어온 프레임에도 업로드가 기록됐다. 위 표는 이 장면의 측정치다.

[프레임 로그 CSV](validation/yokai-vfx/profile.csv) · [이벤트 로그 JSONL](validation/yokai-vfx/profile.jsonl). 콘솔의 `VfxDraws`, CSV의 `vfx_draws`, JSONL의 `weapon_vfx_spawn`으로 재생과 드로 콜을 추적할 수 있다.
