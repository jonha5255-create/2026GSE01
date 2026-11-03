# LowPolyKit 월드 적용과 FPS 개선

검증일: 2026-11-03. 기준 실행 파일은 변경 전 `10c9e4a`에서 보존한 x64 Debug/Release이며, 원시 측정 자료는 [validation/art-world](validation/art-world)에 있습니다.

## 실제 월드에 적용한 내용

Blender 제작 원본을 런타임에서 실행하지 않고, 내보낸 GLB와 `Art/LowPolyKit/data`의 카탈로그·애니메이션 명세를 사용합니다. 원본 `.blend`, 제작 스크립트, GLB는 수정하지 않았습니다. 기존 2.5D 투영과 Actor/SceneGraph 구조 위에 에셋을 연결했습니다.

| 대상 | 연결 방법 |
| --- | --- |
| 플레이어 | 공유 NPC 골격과 모듈러 파츠. 검 / 쌍권총 / 클로 파츠를 현재 요괴 무기 상태에 맞춰 선택 |
| 인간 NPC | 이름이 있는 11개 외형 프리셋과 직업별 프리셋. 걷기·대기·공격·사망 상태 연결 |
| 야생 몬스터 | 아트 몬스터 7종을 랜덤 배치에 연결. 게임 전투 타입과 외형 인덱스를 별도로 관리 |
| 보스 | 지역 보스에는 RedHornOni / FusionBeast / SecurityConstruct, 이야기 수문장에는 StationWarden 연결 |
| 마을 | `building_regions.json`의 7개 지역 건물 목록을 마을 인덱스에 대응 |
| 외곽·성벽 | 폐허 건물, 성벽 구간, 가로등, 나무·바위 GLB 사용 |

NPC 파츠는 공통 골격 원점을 유지합니다. 건물만 기존 필지의 가로·세로 크기에 맞춰 변환하므로 집 소유권과 기존 충돌 판정이 유지됩니다. 자연물은 도로를 피한 보행 가능 지점에 장식으로 배치하며 별도 충돌체를 추가하지 않습니다. 도로·바닥·미니맵·한글 UI·동행 요괴 및 스킬 이펙트는 기존 렌더링을 함께 사용합니다.

공격 클립은 기존 전투 상태와 진행률을 사용하고, 몬스터의 공격 클립 impact 시점을 현재 게임의 준비 동작 시간에 맞춰 재생합니다. 피해 판정은 게임 로직이 담당합니다. 외형이 늘었다고 공격 판정 타입을 임의로 7개로 바꾸지 않습니다.

화면 예시: [마을](validation/art-world/living-world-town.png), [성벽과 보스 구역](validation/art-world/living-world-expedition.png), [원거리 스킬](validation/art-world/yokai-ranged-skill.png).

## 2FPS 현상의 분석

변경 전 Debug 실행에서 약 3FPS를 재현했습니다. 예열 후 평균 갱신 구간은 185.64ms, 렌더 구간은 107.05ms였습니다. draw call은 11회였으므로 draw call 수만으로 설명되는 문제가 아니었습니다.

반복 비용이 큰 부분은 다음과 같았습니다.

1. 장면의 객체 조회에서 전체 Actor에 RTTI 검사와 부모 상태 조회를 반복했습니다. 정렬 비교 중에도 위치·부모 조회가 반복됐습니다.
2. 주민 이동은 목적지까지의 긴 시야/충돌 검사를 매 프레임 반복했습니다. 7개 마을의 주민이 화면 밖에서도 동작하므로 비용이 누적됐습니다.
3. 마을 소식 집계가 주민 전체를 매 프레임 재조회했습니다. 정적 배경도 불필요한 Update 순회와 정렬에 들어갔습니다.
4. 에셋 연결 과정에서 시험한 CPU 스키닝 경로는 자세마다 정점 변환·면 정렬·큰 메시 전송이 추가돼 충분한 성능을 내지 못했습니다. 최종 월드 경로는 이를 GPU 스키닝으로 교체했습니다.
5. 별도로 이 PC에서는 VSync 요청 시 Swap 대기가 평균 약 43ms 발생했습니다. CPU 병목을 줄여도 VSync를 유지하면 관측 FPS가 약 21로 제한됐습니다.

측정 구간은 CPU 벽시계 시간입니다. GPU 타이머 쿼리로 측정한 GPU 실행 시간은 아니며, RenderCPU에도 드라이버 대기가 포함될 수 있습니다.

## 적용한 최적화

### 메시·애니메이션

- GLB 파일과 bind bounds를 시작 시 한 번 읽고 공유합니다. 매 프레임 디스크 접근이나 GLB 재파싱을 하지 않습니다.
- 선택된 파츠의 정점·색상·스킨 가중치를 불변 VBO에 저장합니다. 골격이 움직여도 VBO를 다시 만들지 않습니다.
- 새 `ArtModel.vs/fs`에서 GPU 스키닝과 투영·음영을 처리합니다. 매 프레임에는 작은 뼈대 행렬 팔레트만 전달합니다.
- 같은 모델·클립·시간의 자세는 공유 캐시를 사용합니다. 자세는 30Hz로 샘플링하고, 방향 회전은 연속 값으로 처리합니다.
- 모델 내부 면의 가림은 depth buffer로 해결해 CPU 삼각형 정렬을 없앴습니다. 모델 사이의 순서는 기존 SceneGraph가 유지합니다.
- GPU 모델 캐시는 최대 128개 / 64MiB, 자세 캐시는 모델 파일당 최대 64개입니다. 교체 시 GPU 자원을 해제합니다. 테스트 시작 마을의 모델 캐시 실측은 약 30.9MiB였습니다.
- 화면 밖 객체는 모델 제출 전에 제외합니다. 기존 2D 메시 배칭·디스크 캐시·후처리는 유지합니다. 미리보기의 CPU 투영 경로도 삼각형별 제출 대신 packed model 단위로 제출합니다.

GPU 팔레트는 OpenGL 3.3의 최소 uniform 한도를 고려해 모델당 56행렬까지 허용합니다. 현재 에셋은 이 범위 안에 있습니다. 더 큰 골격은 팔레트 분할 또는 texture-buffer 방식으로 확장해야 합니다.

### 시뮬레이션·장면

- Actor ID 조회용 해시 인덱스와 부모 포인터를 사용합니다. 정렬 키는 정렬 전에 한 번 계산합니다.
- 타입별 Query 후보 목록을 캐시하고 생성·실제 제거 시 무효화합니다. 부모 비활성화와 삭제 대기는 조회 시점에 검사합니다.
- 배경 Actor는 `tickEnabled=false`로 불필요한 Update를 생략합니다. NPC의 상태 갱신 자체는 계속 매 프레임 실행합니다.
- 긴 이동 경로의 시야 검사를 약 0.20~0.27초 재사용합니다. 목적지가 12단위 이상 바뀌거나 짧은 이동이 막히면 재검사합니다. 실제 다음 이동 구간의 충돌 검사는 매 프레임 유지합니다.
- 마을 소식 요약은 1초마다 집계합니다. 주민·편지·이사 로직의 프레임 갱신은 유지합니다.

### 프레임 출력

기본값을 **VSync 해제 + 60FPS 제한**으로 변경했습니다. `FramePacer`는 Windows 고해상도 waitable timer를 사용하며 지원하지 않으면 일반 타이머로 대체합니다. 이미 프레임 예산을 초과하면 추가로 기다리지 않습니다. `--vsync=1`과 `--fps-limit=0`으로 각 동작을 별도로 비교할 수 있습니다.

## 측정 결과

동일 PC, AMD Radeon(TM) Graphics / OpenGL 3.3, 1280×800, seed=2026, 시작 위치에 정지, 모니터 서버 끔. 각 CSV의 첫 30프레임을 제외했습니다. 변경 전은 180프레임, 변경 후 기본 Release는 600프레임, 무제한 Release는 1800프레임, Debug는 180프레임입니다. NPC가 실제 경과 시간으로 행동하므로 각 실행의 시뮬레이션 시점·객체 수는 완전히 같지는 않습니다.

| 실행 | FPS | 평균 프레임 ms | 프레임 p95 ms | RenderCPU ms | UpdateCPU ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| 변경 전 Debug / VSync 1 | 3.02 | 331.31 | 389.77 | 107.05 | 185.64 |
| 변경 후 Debug / 기본 설정 | 24.72 | 40.45 | 64.51 | 19.33 | 20.69 |
| 변경 전 Release / VSync 1 | 21.36 | 46.81 | 48.08 | 5.59 | 6.33 |
| GPU 경로 Release / VSync 1 진단 실행 | 21.44 | 46.65 | 47.86 | 2.34 | 1.22 |
| 변경 후 Release / 기본 60FPS 제한 | 60.00 | 16.67 | 17.23 | 1.71 | 0.77 |
| 변경 후 Release / 제한 없음 | 301.95 | 3.31 | 3.67 | 1.65 | 0.42 |

Debug의 최적화 해제와 디버그 런타임 설정은 유지했습니다. Debug도 약 8.2배 개선됐지만 60FPS는 아닙니다. 일반 플레이 성능은 Release x64로 확인하세요. 무제한 수치는 해당 시작 장면의 처리 여유를 확인한 결과이며 전 지역·대규모 전투의 보장 FPS를 뜻하지 않습니다.

기본 Release의 예열 후 프레임당 전송량은 평균 약 335.8KiB이고 GPU 모델 재업로드는 0회입니다. 원래 절차형 모델 실행은 약 438.4KiB였으므로, 더 많은 면을 가진 실제 에셋을 적용하면서도 프레임 전송량을 줄였습니다.

draw call은 기본 시작 장면에서 약 172~180회로 늘었습니다. 각 모델은 한 번의 `glDrawArrays`로 제출하고, 그림자·라벨 등 2D 요소와의 순서를 유지하기 때문에 호출이 나뉩니다. 면마다 GL draw를 호출하지 않습니다. 이번 변경은 CPU 스키닝·메시 재전송·시뮬레이션 반복을 제거한 결과이며, draw call 감소로 얻은 성능이라고 해석하지 않습니다. 이후 동일 모델의 GPU 인스턴싱을 추가할 여지는 있습니다.

첫 프레임 비용은 예열 통계에서 제외했지만 원시 CSV에 보존했습니다. 최종 기본 Release 첫 프레임은 157.7ms, Debug는 2463.2ms로 초기 경로·충돌 데이터와 GPU 자원 준비 비용이 남습니다. 이는 시작 전 GLB 로딩 시간을 모두 포함하는 프로세스 전체 기동 시간 측정은 아닙니다. 첫 진입 로딩 개선은 별도 작업 대상입니다.

## 로그와 재측정

```powershell
# 일반 플레이: VSync 0 / 60FPS 제한 / 로컬 모니터 켜짐
./x64/Release/SimpleGame.exe

# 같은 시작 장면에서 600프레임 검증
./x64/Release/SimpleGame.exe --profile-test --profile-frames=600 --seed=2026 --no-monitor

# 제한 없이 CPU/렌더 처리 성능 확인
./x64/Release/SimpleGame.exe --profile-test --profile-frames=1800 --seed=2026 --no-monitor --fps-limit=0

# Swap 대기 분리 확인
./x64/Release/SimpleGame.exe --profile-test --seed=2026 --no-monitor --vsync=1 --fps-limit=0

./tools/Analyze-RenderLog.ps1 -Csv ./x64/Release/Logs/실제파일명.csv -SkipWarmupFrames 30
```

`Logs/render-*.csv`에는 `update_cpu_ms`, `render_cpu_ms`, `present_ms`, `pacing_ms`를 나눠 기록합니다. `pose_builds/hits`, `art_pose_ms`는 자세 캐시, `gpu_model_uploads/resident_bytes/entries`는 GPU 모델 캐시입니다. `mesh_generations`는 기존 2D 메시 캐시 지표이며 GPU 모델 업로드와 구분합니다. JSONL의 `world_art_ready`, `world_art_error`, `gpu_model_upload` 이벤트로 로딩·캐시 입주를 추적합니다.

월드 모니터 JSON에는 `appearancePreset`, `artCreature`를 추가했습니다. 기존 필드를 유지하는 추가 필드이며, 인간의 `artCreature=-1` 등 미지정 값은 해당 외형 분류를 사용하지 않음을 뜻합니다.

## 검증과 남은 범위

- Release x64 게임플레이·장면·렌더러 통합 검사: **231 PASS / 0 FAIL**. GPU/CPU 투영·뼈대 이동·투명도·그리기 순서 비교와 불변 메시 재사용 검사 포함.
- 에셋 검사: **82 GLB / 238,007 정점 / 합산 330 클립**. 파일 간 중복 클립도 합산한 숫자입니다. 9가지 미리보기 동작과 프레임 변화 검사 통과.
- 캡처의 OpenGL 오류 0, HDR/후처리·홀수 해상도 리사이즈 검사 통과.
- x64·x86 / Debug·Release 네 가지 빌드 구성 통과. x64 Release, x86 Debug/Release의 GLFW 문맥·리사이즈·종료 검사 통과.
- loopback 모니터에서 실제 JSON 수신과 에셋 식별 필드 확인: 마을 7개, NPC 210명, 몬스터 72마리. [결과](validation/art-world/monitor-check.json).

새 에셋은 Blender에서 GLB로 내보내고 해당 카탈로그와 manifest를 갱신한 뒤 게임을 다시 실행해 적용합니다. 미리보기는 기존 R 다시 읽기를 지원하지만 실제 월드의 실행 중 hot reload는 구현하지 않았습니다. 새로운 외형을 카탈로그에 넣는 것과 새 전투 행동을 만드는 것은 별도입니다. 새 몬스터 행동·요괴 진화 단계별 전용 파츠는 추가 아트와 게임 매핑이 필요합니다.

주요 연결 코드는 `WorldArt.cpp`, GPU 제출은 `PrototypeRenderer::ModelMesh`, 스키닝은 `Shaders/ArtModel.vs`, 프레임 제한은 `FramePacer.cpp`에 있습니다. 이 문서의 측정치는 [원시 CSV와 요약 JSON](validation/art-world)에 함께 저장했습니다.
