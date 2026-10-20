# 캐릭터 리깅 · 몹 7종 · 키프레임 애니메이션

달리기 revision 5: NPC 바지의 골반 셸을 허벅지 가중치에서 분리하고 팔 길이를 약 26% 늘렸습니다. 소매에 팔꿈치 굽힘용 링을 추가했으며, 새 바인드 포즈에 맞춰 NPC 20개 클립을 다시 구웠습니다. 2족 Run은 작은 골반 회전과 상체 반대 회전, 굽힌 팔 반동을 사용합니다. 4족은 Walk(4박), Run(대각선 트로트), Sprint(회전 갤럽)로 구분합니다. 최신 달리기 검증은 `data/run_revision_validation.json`, 측면 미리보기는 `npc_run_side.gif`와 `quadruped_run_side.gif`입니다.

몬스터 아트 revision 4: `doc/몬스터 컨셉 아트_ 2.5D 오픈월드 RPG.png`를 기준으로 인간형 4종 + 4족보행 3종으로 교체했습니다. 기존 잔향귀·시체 사냥개·석괴·늑대·멧돼지·곰·사슴 초안의 메시/골격/액션과 GLB는 활성 세트에서 제거했습니다. 교체 전 원본은 `revisions/Characters_before_monster_concept.blend`에 보관합니다. NPC 리깅/파츠/20개 액션은 유지합니다.

원본: `Characters_Rigged.blend`. 건물/자연물 원본 `LowPolyKit.blend`는 보존합니다. 게임 코드·빌드·적용 작업은 수행하지 않았습니다.

## 결과 파일

| 경로 | 내용 |
| --- | --- |
| `rigged/NPC_LOOK_000.glb` ~ `010.glb` | 기존 이름 있는 주민 11명, 공통 골격과 20개 클립 포함 |
| `rigged/NPC_ModularParts_Library.glb` | NPC 파츠 28개를 각각 이름 있는 스킨 메시로 보관 |
| `rigged/SK_MOB_00_*.glb` ~ `06_*.glb` | 몹 7종, 종별 골격과 12개 클립 포함 |
| `data/animation_manifest.json` | 클립 이름·프레임·길이·반복 여부·속도·루트 이동·타격/발사 시점 |
| `data/rigged_part_catalog.json` | 슬롯 인덱스 → 스킨 메시 노드 이름과 라이브러리 파일 |
| `data/rig_validation.json` | 가중치와 보행 접지 목표 검사 |
| `data/glb_animation_validation.json` | 실제 내보낸 GLB의 스킨/동작/시간/가중치 검사 |
| `data/glb_roundtrip_validation.json` | NPC와 철문 견마 GLB를 Blender에 다시 가져와 동작 변형 확인 |
| `data/monster_replacement.json` | 제거한 이전 몹 파일과 새 참조 이미지 |
| `data/monster_rig_validation.json` | 새 몹 4+3종의 가중치 및 접지 검사 |
| `previews/creature_lineup.png` | 몹 7종 모델 시안 |
| `previews/npc_walk.gif`, `npc_run.gif` | 2족 걷기·달리기 미리보기 |
| `previews/quadruped_walk.gif`, `quadruped_run.gif` | 새 철문 견마의 4족 걷기·달리기 미리보기 |
| `previews/npc_run_side.gif`, `quadruped_run_side.gif` | v5 달리기 측면 확인용 |
| `previews/quadruped_sprint.gif` | 별도 4족 전력질주/갤럽 |
| `previews/npc_sword.gif`, `npc_bow.gif` | 검·활 공격 미리보기 |
| `previews/npc_jump.gif`, `quadruped_turn.gif` | 점프·방향 전환 미리보기 |
| `previews/weapon_attack_keyposes.png` | 7개 공격 계열의 준비·타격/발사·복귀 자세 |

기존 정적 GLB는 `meshes/`에 그대로 있습니다. 애니메이션 적용 작업에서는 `rigged/` 파일을 사용하십시오. NPC 개별 GLB는 조합 확인용 완성 프리셋이며, 게임에서 메시 공유를 유지하려면 공통 라이브러리에서 슬롯별 메시를 선택해 같은 골격에 연결하십시오. 라이브러리 파일의 모든 의복/헤어 변형을 동시에 표시하면 서로 겹칩니다.

## NPC 골격과 파츠

공통 19본: root, pelvis, spine, chest, neck, head, 좌우 upper_arm/forearm/hand, 좌우 thigh/shin/foot, bow_string. 손가락/얼굴 리깅은 생략한 로우폴리 몸체 리그입니다. 긴 머리는 head를 따라가며 별도 물리 본은 없습니다.

몸통·팔다리는 관절 주변에 가중치를 나눴고, 머리·헤어·장비는 해당 본을 따라갑니다. 시위는 별도 본 키프레임으로 당겼다가 놓습니다. 검과 가위형 무기에는 손목 회전을 추가했습니다. 블렌더 편집 단계에서 두 관절 IK로 접지 자세를 계산했으며, 결과는 프레임마다 FK 본 위치/회전 키로 구웠습니다. 런타임 IK나 제작용 제약 조건 없이 GLB에서 재생할 수 있습니다.

기존 파츠 인덱스를 유지합니다. prop의 0 활, 1 창, 2 기록장 뒤에 3 검, 4 쌍권총, 5 클로, 6 가위형 쌍검을 추가했습니다. 활은 리깅 버전에서 왼손으로 옮기고 손잡이 중심을 손 위치에 맞췄습니다. 쌍권총·클로·가위형 쌍검은 한 메시 안에서 좌우 손에 각각 가중치를 줍니다. 기록장은 비전투 소품입니다.

## NPC 클립 20개

- `NPC_Idle`: 호흡 대기, 반복.
- `NPC_Walk`, `NPC_Run`: 제자리 보행, 반복.
- `NPC_Walk_RootMotion`, `NPC_Run_RootMotion`: 발 접지 검사용/이동용 루트 이동 버전.
- `NPC_Turn_L45`, `R45`, `L90`, `R90`: 발을 나눠 옮기며 회전하는 전환 동작.
- `NPC_Jump`: 준비 → 이륙 → 체공 → 착지 → 복귀.
- `NPC_Attack_Sword`, `SwordHeavy`, `Spear`, `Bow`, `DualPistol`, `Claw`, `Scissors`, `Unarmed`: 무기별 공격 8개.
- `NPC_Hit`, `NPC_Death`: 피격/사망.

공격 클립은 무기를 자동 교체하지 않습니다. 적용 작업에서 해당 prop 메시를 골라 표시해야 합니다. `animation_manifest.json`의 이벤트는 제안된 연출 시점이며 피해 판정이나 발사체 생성 기능은 구현하지 않았습니다. 활 발사 이벤트는 시위를 놓는 시점과 맞추고, 쌍권총과 연속 공격은 복수 이벤트를 기록했습니다.

## 몹과 골격

| 인덱스 | 이름 | 보행 | 공격 표현 |
| --- | --- | --- | --- |
| 0 | 종이상자 요괴 / PaperBoxYokai | 2족, 18본 | 상자 얼굴·접힌 날개, 양팔 타격 |
| 1 | 역무원 요괴 / StationWarden | 2족, 18본 | 제복·제모·신호봉, 전진/내려찍기 |
| 2 | 붉은 뿔 요괴 / RedHornOni | 2족, 18본 | 붉은 가면·굽은 뿔·손톱, 발톱 공격 |
| 3 | 보안 병기 / SecurityConstruct | 2족, 18본 | 백색 장갑·검은 관절·붉은 핵, 주먹/양팔 공격 |
| 4 | 철문 견마 / IronHound | 4족, 21본 | 관절 장갑·기계 턱·등 돌기, 물기/옆 물기 |
| 5 | 혼종체 / FusionBeast | 4족, 21본 | 노출 조직·뼈 장갑·촉수, 머리로 들이받기 |
| 6 | 공허 추적자 / VoidStalker | 4족, 21본 | 암자색 장갑·가시·붉은 눈, 돌진 물기 |

몹은 완성형 메시 1개와 종별 골격 1개입니다. 4족은 몸통/어깨/골반·목·머리·턱·꼬리와 앞뒤 다리를 구분했습니다. Walk는 뒷왼발→앞왼발→뒷오른발→앞오른발의 4박 보행입니다. Run은 앞왼발+뒷오른발 / 앞오른발+뒷왼발을 교대로 쓰는 트로트입니다. Sprint는 앞왼발→앞오른발→뒷오른발→뒷왼발의 회전 갤럽으로, 몸을 모으고 뻗는 두 공중 구간과 척추·목·꼬리 반동을 별도로 키프레임화했습니다. NPC와 인간형 몹은 좌우 다리 반대 위상과 팔 반동을 사용합니다. 보드의 전체 몬스터 수십 종을 모두 제작한 것이 아니라 위의 7종을 대표로 선정했습니다.

각 몹은 접두사 `MOB_00_` ~ `MOB_06_` 뒤에 다음 기본 12개 클립을 가집니다: Idle, Walk, Walk_RootMotion, Run, Run_RootMotion, Turn_L90, Turn_R90, Jump, AttackPrimary, AttackSecondary, Hit, Death. 4족 3종에는 Sprint, Sprint_RootMotion을 추가했습니다. 모션은 총 110개입니다: NPC 20개 + 인간형 몹 4×12개 + 4족 몹 3×14개.

## 적용 기준

- 단위: 미터. 제작 파일은 Z 위/-Y 전방, GLB는 Y 위/+Z 전방.
- 30 fps. GLB의 실제 시간은 초 단위이며 런타임 fps와 무관하게 시간으로 재생하십시오.
- root는 골격 최상위 본입니다. 보행 제자리 버전은 누적 전진이 없고, RootMotion 버전만 전진량을 가집니다. 같은 이동량을 Actor 이동과 본 이동 양쪽에 중복 적용하지 마십시오.
- 루트 이동 클립 끝은 전진한 상태입니다. 반복 시 루트 변위를 누적하거나 추출해야 합니다. 제자리 버전은 시작/끝 자세를 맞춘 반복 클립입니다.
- 방향 전환은 0→45/90도 또는 0→90도 회전을 포함하는 단발 클립입니다. 재생이 끝난 회전은 다음 자세/Actor 방향에 반영해야 합니다. 현재 방향 차이에 맞는 블렌딩·상태 전환 로직은 다른 작업에서 구현합니다.
- GLB에는 이름 있는 클립들이 포함됩니다. Blender Action/NLA 이름과 클립 이름은 동일합니다. NLA 트랙은 편집 원본에서 음소거해 두었으므로 Action Editor에서 액션을 골라 재생하십시오.
- 기본 파일은 NPC를 표시합니다. 몹을 편집하려면 `05_RIGGED_CHARACTERS` 안에서 해당 몹 컬렉션의 표시를 켜고 NPC 파츠를 숨기십시오.
- 미리보기 GIF는 확인용 저해상도 샘플입니다. 실제 키프레임은 GLB/Blender 안에 30 fps로 저장됩니다.

## 검증의 범위

가중치 누락/합계, 실제 GLB의 스킨과 애니메이션 채널, 시간 순서, 유한 좌표, 걷기·달리기의 접지 중 발목 FK 위치를 검사합니다. 접지 검사는 루트 이동을 포함한 평지 기준입니다. 프리뷰로 기본 실루엣·관절·무기 동작도 확인합니다. 게임의 경사 지형, 충돌 캡슐, 전투 거리, 상태 전환과 블렌딩 결과는 적용 작업에서 별도로 맞춰야 합니다.

제작 레시피 실행 순서: `author_rigs_and_creatures.py` → `finalize_rig_scene.py` → `render_rig_previews.py` → 시스템 Python의 `package_animation_previews.py`, `validate_rig_exports.py`. 앞의 세 파일은 Blender 백그라운드 Python으로 실행합니다. 수동으로 수정한 원본은 재생성 전에 보관하십시오.

최신 몹을 재생성하려면 기본 리깅 생성/정리 뒤 `replace_concept_creatures.py`를 반드시 적용하고 프리뷰·패키징·검증을 실행하십시오. `ART_PREVIEW_MONSTERS_ONLY=1` 환경 변수로 미리보기 제작을 실행하면 기존 NPC 렌더를 재사용하고 새 몹만 렌더합니다. 기존 코드상의 몬스터 타입 0/1/2와 이번 아트 인덱스 0~6은 서로 다른 식별자입니다. 런타임의 타입 매핑은 후속 적용 작업에서 명시적으로 처리해야 합니다.

v5 최종 제작 단계는 `refine_running_v5.py` → `render_running_v5.py` → `package_animation_previews.py` → `validate_rig_exports.py`입니다. 팔 길이를 중복해서 늘리지 않으며 기존 메시 이름/슬롯 인덱스를 유지합니다. v5는 스킨 에셋 `rigged/`와 `Characters_Rigged.blend`에 적용되며 기존 정적 환경 세트는 유지합니다.

동작 구조 참고: [개 보행 전환과 발 착지 순서](https://journals.biologists.com/jeb/article/216/12/2257/11423/Gait-transitions-and-modular-organization-of), [달리기 중 팔 반동과 골반 회전](https://journals.biologists.com/jeb/article/217/14/2456/12120/The-metabolic-cost-of-human-running-is-swinging). 연구의 모션 캡처를 복제한 결과물이 아니라 3등신 체형에 맞춘 제작 키프레임입니다.
