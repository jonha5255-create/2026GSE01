# 렌더러 배칭, 메시 캐시, 성능 분석

구현 및 측정: [드로 콜 최적화 리포트](docs/performance/2026-09-22-drawcall-report.md).

## 렌더링 구조

Actor/SceneGraph의 그리기 순서를 유지하면서 PrototypeRenderer가 메시 참조와 변환·색상을 모읍니다. 불변 정점은 공용 GPU texture-buffer 아틀라스에 저장하고 삼각형 참조와 오브젝트별 인스턴스 정보만 매 프레임 전송합니다. Flush에서 glDrawArraysInstanced로 제출합니다. 서로 다른 메시도 합칠 수 있으며 투명 도형을 재질별로 재정렬하지 않습니다.

사각형, 선, 원, 글로우, 글자, 동적 삼각형은 단위 메시를 변환해 재사용합니다. Floor/Building의 CachedMesh 콜백은 메모리·디스크 캐시에 모두 없을 때만 실행합니다. 공간 왜곡처럼 변화하는 도형은 단위 삼각형 변환으로 표현합니다. 액터와 캐시 자원 수명은 분리되어 있습니다.

GPU/배치 용량이나 아틀라스 페이지가 달라지면 순서대로 분할합니다. 측정 장면은 scene 1 + post 9 + UI 1 = 11회이며 모든 장면의 고정 상한은 아닙니다. 후처리를 꺼도 최종 합성은 필요하므로 3회입니다.

## 캐시 운용

- 실행 파일 옆 MeshCache/*.gmesh에 저장합니다. 쓰기 불가 시 메모리 렌더링은 계속됩니다.
- 파일 스키마, 키, 크기, 체크섬, 정점 값 범위를 검증하며 손상/구버전 파일은 재생성합니다.
- 메모리 캐시는 256개 / CPU 정점 32 MiB 목표로 프레임 시작 시 LRU 정리합니다. GPU 정점 사본과 재구성 임시 메모리는 별도입니다. 기본 도형은 유지합니다.
- 디스크는 64 MiB / 1024파일 목표로 시작 시와 64회 저장마다 오래된 파일을 정리합니다. 일시 초과할 수 있습니다.
- 같은 키의 내용은 불변입니다. 생성 알고리즘 변경 시 geometry-v2 네임스페이스나 메시 키 버전을 바꾸세요.
- --rebuild-mesh-cache는 파일 읽기를 우회하여 사용되는 메시를 재생성합니다.
- 생성 콜백에서 Begin/Flush/중첩 CachedMesh를 호출하지 마세요.
- 새 메시 유입/퇴거 시 아틀라스를 재구성합니다. 새 청크에서는 생성과 파일 I/O가 발생할 수 있습니다.

## 콘솔 및 지속 로그

FrameProfiler는 실제 GL draw 호출 지점을 계수합니다. glClear/업로드는 drawcall에 포함하지 않습니다. 새 GL draw 경로에도 RecordDrawCall(stage)를 추가해야 합니다. 빌드 제외 참고 파일 Renderer.cpp는 집계 대상이 아닙니다.

콘솔은 1초마다 FPS, 프레임 간격, drawcall last/avg/min/max, Scene/Post/UI, 삼각형 인스턴스, 생성/로드 횟수, CPU 렌더 시간, 업로드량을 출력합니다. 후반 항목은 직전 프레임 값이므로 초기 생성 비용은 CSV에서 확인하세요.

실행 파일 옆 Logs에 세션별 CSV(매 프레임)와 JSONL(이벤트)을 저장합니다. 파일명은 UTC입니다. 1초마다 및 정상 종료 시 flush합니다. 자동 로그 삭제 정책은 없으므로 장시간 측정 후 보관/정리하세요. 로그 파일 열기 실패 시 콘솔 경고를 출력합니다.

```powershell
.\x64\Release\SimpleGame.exe --profile-test --seed=2026 --rebuild-mesh-cache
.\x64\Release\SimpleGame.exe --profile-test --seed=2026
.\tools\Analyze-RenderLog.ps1 -Csv .\x64\Release\Logs\render-실제파일명.csv
```

--profile-test는 일반 루프 180프레임 후 종료합니다. 분석 스크립트는 기본 첫 프레임을 정상 상태 통계에서 제외하되 전체 합계와 첫 프레임 비용도 출력합니다. --smoke-test는 회귀 검사이며 FPS 벤치마크가 아닙니다.

FPS/FrameMs는 타이머와 Swap 대기를 포함한 표시 간격입니다. RenderCPU는 Swap 이전 CPU 구간으로 GPU 시간이 아니며 드라이버 대기가 섞일 수 있습니다. 16ms 타이머/드라이버 기본 동기화 환경에서 drawcall 감소가 FPS 증가를 보장하지 않습니다.

## 독립 셰이더와 배포

| 파일 | 역할 |
| --- | --- |
| Shaders/Scene.vs | 메시/인스턴스 texture-buffer 조회, 변환·색상·UV |
| Shaders/Scene.fs | 글꼴 아틀라스, 선형 색, HDR 발광 |
| Shaders/Fullscreen.vs | 전체 화면 삼각형 |
| Shaders/Blur.fs | 블러, HDR 하이라이트 추출 |
| Shaders/Composite.fs | 블룸·비네트·가장자리 흐림·톤 매핑 |

ShaderProgram이 실행 파일 기준 Shaders 경로에서 UTF-8 파일을 읽습니다. MSBuild가 셰이더를 복사하며 배포 시 Shaders 폴더가 필요합니다. 누락/컴파일 실패 시 초기화를 중단합니다. C++ 내 셰이더 폴백과 런타임 핫 리로드는 없습니다. SolidRect.vs/.fs는 참고용입니다.

## 검증

--smoke-test는 캐시/직접 도형과 배칭/분리 제출 픽셀 일치, 변환·투명 순서, 파일 round-trip/손상 검출, 퇴거 후 재로드, descriptor/atlas-page 분할, LRU, GL 오류, HDR/효과/리사이즈, 레벨 1 게임플레이를 검사합니다.
