# FreeGLUT → GLFW 전환과 성능 검증

> 아래 표는 전환 당시의 기록입니다. 현재 기본값은 VSync 해제 + 고해상도 타이머를 사용하는 60FPS 제한으로 변경했습니다. 최신 CPU 병목 분석과 실제 에셋 적용 결과는 [월드 적용 보고서](LowPolyKit_월드적용과_FPS_개선.md)를 참고하세요.

## 실행·빌드

GLFW 3.4 전체 소스·헤더·라이선스를 `SimpleGame/Dependencies/GLFW/glfw-3.4/`에 포함했습니다. `glfw.vcxproj`가 구성과 아키텍처에 맞는 정적 라이브러리를 만들고 게임 프로젝트에서 참조합니다. 빌드 중 네트워크 다운로드나 GLFW 설치가 필요하지 않습니다.

Visual Studio 2022 C++ Desktop 도구와 Windows SDK를 설치한 후 `SimpleGame.sln`을 열어 Debug/Release 및 x86/x64를 선택합니다. Developer PowerShell에서는 다음과 같이 빌드합니다.

```powershell
MSBuild SimpleGame.sln /p:Configuration=Release /p:Platform=x64
./x64/Release/SimpleGame.exe

# 전체 구성 빌드 및 실제 GLFW 창 검사
./tools/build_glfw_matrix.ps1 -RuntimeChecks

# 개별 검사
./x64/Release/SimpleGame.exe --platform-test --no-monitor
./x64/Release/SimpleGame.exe --smoke-test --no-monitor
./x64/Release/SimpleGame.exe --asset-smoke-test --no-monitor
```

GLFW는 소스로 커밋하고 빌드 산출물은 제외합니다. 별도 GLFW DLL은 필요하지 않습니다. 기존 GLEW DLL은 아키텍처별로 자동 복사합니다. 다른 개발자 PC의 절대 경로나 사용자 라이브러리 설치에 의존하지 않도록 라이브러리 경로를 ProjectDir 기준으로 바꿨습니다. 게임의 GDI 폰트와 WinSock 모니터는 Windows용이므로 이번 검증의 다른 환경 범위는 Windows의 x86/x64 및 Debug/Release입니다.

## 창·입력·루프

`GlfwPlatform.h/.cpp`에서 GLFW 수명, OpenGL 3.3 Core/더블 버퍼 문맥, 창, 이벤트와 입력을 관리합니다. 기존 게임·렌더러 API는 유지합니다.

- 명시적인 `Poll → Update → Render → Swap` 루프. 창 닫기와 ESC는 close flag를 설정하고 루프 밖에서 모니터·렌더러·문맥을 순서대로 정리합니다.
- 기본 VSync 요청은 1입니다. `--vsync=0`으로 해제할 수 있습니다. 실제 주기는 모니터·드라이버 설정에도 영향을 받습니다.
- 이전 FreeGLUT 타이머는 갱신이 끝난 시점에서 다음 갱신을 16ms 뒤에 예약했습니다. 새 루프는 별도의 갱신 후 16ms 예약을 없애고 Swap의 동기화 대기로 프레임을 조절합니다.
- 최소화 중에는 렌더링을 생략하고 이벤트 대기와 시뮬레이션 갱신을 계속합니다. 복원 후 이동 키가 고착되지 않도록 포커스 상실 시 눌린 키를 한 번씩 해제합니다.
- GLFW 반복 키 이벤트는 무시하고 기존 1/2/3, WASD, TAB, SPACE, ESC, 대괄호 등을 매핑합니다.
- 마우스 창 좌표를 실제 framebuffer 픽셀로 변환하여 고해상도 화면에서도 클릭 대상 선택을 맞춥니다. 렌더 타깃은 framebuffer-size 콜백으로 갱신합니다.
- headless `--monitor-test`와 CPU `--self-test`, `--asset-test`는 GLFW 초기화 없이 실행됩니다.

## 비교 조건과 결과

변경 전 실행 파일을 별도 경로에 보존했습니다. 같은 AMD Radeon Graphics / OpenGL 3.3 드라이버, Release x64, 1280×800, seed=2026, 모니터 서버 끔, `--profile-test` 180프레임을 각 3회 실행했습니다. 각 실행의 처음 30프레임을 제외한 150프레임을 집계했습니다. 동일 PC에서 순차 측정했으며 라이브러리 API만의 순수 마이크로벤치마크는 아닙니다.

| 지표: 3회 평균 | FreeGLUT | GLFW |
| --- | ---: | ---: |
| FPS | 44.98 | 68.65 |
| 프레임 평균 | 22.232 ms | 14.580 ms |
| 프레임 P95 | 24.734 ms | 17.264 ms |
| 렌더 CPU 평균 | 5.705 ms | 5.779 ms |
| 시뮬레이션 CPU 평균 | 6.132 ms | 6.214 ms |
| draw calls/frame | 11 | 11 |
| 워밍업 이후 메시 생성 | 0 | 0 |

관측 FPS는 약 52.6% 증가했고 프레임 시간은 약 34.4% 줄었습니다. 렌더·갱신 CPU 시간은 약 1.3% 차이로 같은 수준이었습니다. 렌더러·배칭·게임 로직을 바꿔서 얻은 결과가 아니라, 프레임 대기와 이벤트 루프 변경까지 포함한 결과입니다. 이전 버전은 드라이버 기본 swap 설정과 16ms 타이머, 새 버전은 명시적인 VSync 요청 1을 사용했으므로 이를 GLFW 자체의 연산 속도 향상으로 해석하지 않습니다. NPC 시뮬레이션은 실제 경과 시간으로 움직이므로 두 버전의 180프레임 동안 장면의 세부 시점은 조금 다릅니다. 원시 로그에 인스턴스 수도 보존했습니다.

원시 로그와 재현 스크립트:

- `doc/validation/glfw-migration/FreeGLUT-run1..3.csv/.jsonl`
- `doc/validation/glfw-migration/GLFW-run1..3.csv/.jsonl`
- `doc/validation/glfw-migration/summary.csv`
- `tools/compare_framework_profiles.ps1 -BeforeLogs <변경전 Logs> -AfterLogs <변경후 Logs>`

## 회귀 검증

실제 OpenGL 문맥, 키 반복 차단, 포커스 해제, DPI 좌표 변환, 960×600 / 961×601 / 1280×800 리사이즈와 종료를 검사합니다. 게임 전체 smoke-test와 에셋 애니메이션 smoke-test, 로컬 HTTP 모니터 검사도 수행합니다.

변경 전후 `prototype-companion`, `living-world-town`, `living-world-expanded-map`, `yokai-sword-skill`, `prototype-resized`, `prototype-resized-odd`, `prototype-restored`의 BMP 파일은 SHA-256 해시가 모두 일치했습니다. 기존 장면·후처리·지도·스킬·리사이즈 결과가 동일하게 출력됩니다.

GLFW 공식 문서: [초기화와 이벤트 루프](https://www.glfw.org/docs/3.4/quick.html), [소스 빌드](https://www.glfw.org/docs/3.4/compile.html), [3.4 소스와 라이선스](https://github.com/glfw/glfw/tree/3.4).

### 최종 확인 결과

- 기준 코드: `ebf3e01`의 FreeGLUT 실행 파일. 최종 빌드는 표준 `x64/Release/SimpleGame.exe`에도 생성했습니다.
- 완전히 분리된 game-obj/glfw-obj 경로에서 Release x64, Debug x64, Release x86, Debug x86를 모두 Rebuild하고 `--platform-test` 실행 통과.
- GLFW 게임 `--smoke-test`, 에셋 `--asset-smoke-test`, `tests/monitor_integration.ps1` 통과. 네 구성의 리사이즈 캡처는 OpenGL 오류 0.
- 바이너리 import 검사에서 `freeglut.dll`과 `glfw3.dll` 의존성 없음. GLEW와 Windows/VC 런타임 의존성은 기존 방식 유지.
- 7개 전후 이미지의 원본 해시 비교: `doc/validation/glfw-migration/pixel-parity.csv`, 전부 동일.
- GLFW 원본 167개 파일과 별도 빌드 프로젝트/설명서를 포함합니다. 컴파일된 lib/obj/PDB와 테스트용 실행 파일은 Git에서 제외합니다.
