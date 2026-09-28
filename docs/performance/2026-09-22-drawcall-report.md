# 드로 콜·메시 재사용 개선 리포트

작성: 2026-09-22. 대상: Actor/SceneGraph 기반 Level 1, OpenGL 3.3.

## 1. 결과

같은 시드의 Release 실행에서 실제 GL draw 호출이 프레임당 66 → 11회(83.3% 감소)로 줄었다. 사용자의 67회 로그와 비교하면 83.6% 감소다. 비교용 기존 실행은 66회였으므로 두 기준을 구분한다. 후처리 9회는 유지했다.

기존 메모리 캐시는 정적 메시를 재사용하지만 메시마다 별도 draw가 필요했고 동적 도형은 정점을 반복 생성했다. 이제 서로 다른 메시를 순서대로 모아서 제출하며, 동적 도형도 단위 메시와 변환값으로 표현한다. 파일 캐시 재실행에서는 180프레임 전체 메시 생성이 0회였다.

정상 상태 FPS는 전후 약 60이다. drawcall 감소만으로 GPU 부하나 프레임 속도가 같은 비율로 개선되었다고 판단할 수 없다.

## 2. 배칭 설계와 선택 이유

일반 메시별 인스턴싱은 같은 메시를 묶지만 현재 장면에는 서로 다른 메시와 알파 블렌딩 순서가 섞여 있다. 재질/메시별 정렬은 2.5D 겹침 결과를 바꿀 수 있으므로 **순서 보존 삼각형 인스턴싱 + vertex pulling**을 구현했다.

1. 캐시 정점을 공통 GPU texture-buffer 아틀라스에 저장한다. 정점당 float4 3개(48바이트)다.
2. 오브젝트마다 affine 변환, tint, UV, emission을 담은 80바이트 descriptor를 만든다.
3. 원래 그리기 순서대로 삼각형 시작 정점과 descriptor 번호를 8바이트 참조로 모은다.
4. glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 삼각형수)로 제출한다. Scene.vs가 정점과 descriptor를 조회한다.
5. 장면과 UI는 별도 배치로 두어 후처리 이후 HUD 합성을 유지한다.

인스턴싱 단위는 액터나 완성 메시가 아니라 **삼각형**이다. 로그의 약 1.2만 인스턴스는 액터 수가 아니다. instanced draw 동작은 [Khronos 공식 정의](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glDrawArraysInstanced.xml)를 따른다.

페이지 변경, descriptor 한도, 65,536 삼각형 참조 한도에서 배치를 분할한다. descriptor는 최대 65,536개이며 GPU 제한으로 더 작아질 수 있다. GL_MAX_TEXTURE_BUFFER_SIZE를 조회해 페이지와 descriptor 용량을 제한한다. 범위 밖 fetch를 피해야 하는 이유는 [Khronos glTexBuffer 명세](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glTexBuffer.xml)에 명시되어 있다.

후처리는 블룸 4 + 가장자리 흐림 4 + 최종 합성 1회다. 이전 패스 결과와 다른 framebuffer가 필요하므로 단순 배칭으로 합치지 않았다. 효과 비활성화 시 scene 1 + 합성 1 + UI 1 = 3회다. 다른 장면이나 용량 초과 상황에서 11회보다 증가할 수 있다.

## 3. 파일 캐시와 재사용

삼각형/사각형/원/글로우 단위 메시를 캐시한다. 선은 사각형 변환, 글자는 사각형과 UV 변경, 움직이는 삼각형은 affine 변환으로 표현한다. 움직임과 색 변화 때문에 정점을 재생성하지 않는다. Floor/Building은 기존 시드·크기 기반 키를 사용한다.

메모리 miss → 파일 검증/로드 → 없거나 잘못되면 builder 실행 → 임시 파일 저장 및 원자적 교체 → GPU 아틀라스 반영 순서다. geometry-v2-layout12와 글꼴 advance 해시를 키 네임스페이스에 넣는다. 생성 코드가 바뀌면 키/네임스페이스도 갱신해야 한다.

magic/version/키/정점 수/파일 길이/FNV-1a 체크섬/유한 값과 범위를 검사한다. 체크섬은 손상 검출용이지 보안 서명이 아니다. 크기 상한은 할당 전에 확인한다. 실행 파일 옆 MeshCache에 저장하며 쓰기 불가 시 메모리 렌더링은 계속된다.

CPU 정점 목표 예산은 256개/32 MiB이고 GPU 사본도 존재한다. 전체 프로세스 메모리가 32 MiB라는 의미가 아니다. 기본 도형은 퇴거하지 않으며 제출 참조를 보호하기 위해 다음 Begin에서 LRU 정리한다. 디스크는 64 MiB/1024파일 목표로 시작 시와 64번 저장마다 오래된 파일을 정리하므로 일시 초과할 수 있다.

## 4. 측정 조건과 원본 자료

- Windows x64 Release, AMD Radeon(TM) Graphics, OpenGL 3.3.0, 1280×800, seed=2026.
- --profile-test 각 180프레임, 별도 입력 없음. 타이머 16ms, swap interval은 드라이버 기본 설정.
- cold는 --rebuild-mesh-cache로 파일 읽기를 우회한 생성/저장 실행이다. OS 파일 캐시를 강제로 비운 실험은 아니다.
- warm은 직후 같은 시드 재실행이다. 최초 프레임 GPU 업로드는 여전히 필요하다.
- 이전 버전 콘솔: 첫 구간 FPS 56.5 / 17.7ms, 이후 60.0 / 16.7ms, 60.1 / 16.6ms. 모든 구간 draw last/avg/min/max=66. 이전 CPU CSV가 없으므로 CPU 전후 개선률은 계산하지 않는다.

| 지표 | 강제 재생성(cold) | 파일 재사용(warm) |
| --- | ---: | ---: |
| drawcall min / avg / max | 11 / 11 / 11 | 11 / 11 / 11 |
| 첫 프레임 간격(ms) | 206.9833 | 85.7554 |
| 첫 프레임 RenderCPU(ms) | 141.5343 | 17.4462 |
| 2~180 프레임 간격 평균 / p95(ms) | 16.5413 / 18.0923 | 16.5937 / 18.0502 |
| 2~180 RenderCPU 평균 / p95(ms) | 1.3950 / 1.7572 | 1.3024 / 1.5778 |
| 전체 메시 생성 / 디스크 로드 | 51 / 0 | 0 / 51 |
| 전체 지오메트리 업로드(bytes) | 1,173,456 | 1,173,456 |
| 2~180 메시 생성 / 지오메트리 업로드 | 0 / 0 | 0 / 0 |
| 전체 배치 분할 | 0 | 0 |

인스턴스 스트림은 매 프레임 약 181~183KB 업로드된다. **지오메트리 업로드 0은 모든 업로드가 0이라는 뜻이 아니다.** 첫 실행 히치가 사라진 것도 아니다. 폰트/셰이더 초기화 전체 시간은 별도 계측하지 않았다. 단일 머신의 짧은 실행이므로 장기 플레이나 GPU 성능으로 일반화하지 않는다.

원본: [cold CSV](captures/render-2026-0922-080021-30904.csv), [cold 이벤트](captures/render-2026-0922-080021-30904.jsonl), [warm CSV](captures/render-2026-0922-080025-13872.csv), [warm 이벤트](captures/render-2026-0922-080025-13872.jsonl).

## 5. 지속 분석 로그

RenderDiagnostics는 Logs/render-UTC시간-PID.csv와 .jsonl을 생성한다. CSV는 완료 프레임마다 기록하고 1초마다 및 정상 종료 시 flush한다. 비정상 종료 직전 버퍼는 유실될 수 있다. 자동 로그 보존 기간 제한은 없으므로 별도 정리가 필요하다.

| CSV 필드 | 의미 |
| --- | --- |
| frame, elapsed_s, frame_ms | 프레임 번호, 누적 시간, 표시 완료 간격 |
| render_cpu_ms, update_cpu_ms | Swap 이전 렌더 CPU 구간, 해당 프레임에 누적된 업데이트 CPU 시간 |
| draw_calls, scene_draws, post_draws, ui_draws | 실제 GL 호출 총계와 단계별 횟수 |
| objects, triangle_instances | 제출 대상 수, 실제 제출 삼각형 참조 수 |
| instance_upload_bytes, mesh_upload_bytes | 스트림/지오메트리 업로드 바이트 |
| cache_hits, cache_misses, mesh_generations | 메모리 조회/생성 횟수 |
| disk_loads, disk_rejects, disk_writes | 파일 로드/검증 거부/저장 횟수 |
| cache_evictions, batch_splits | 메모리 퇴거 및 용량/페이지 분할 |

JSONL은 session, environment, cache_policy, renderer_limits, mesh_loaded/generated/rejected/saved/write_failed/evicted, atlas_rebuilt, resize, key_down/up, slow_frame, session_end 이벤트를 기록한다. tick_ms는 OS 단조 증가 시간이고 파일명은 UTC다. 아틀라스 재구성 시 페이지 수와 resident bytes도 기록한다. 입력 기록은 게임 창에서 발생한 이벤트다. 보관된 측정 로그는 session_end 추가 전 캡처이므로 해당 이벤트만 없다.

재현 명령은 [RENDERING.md](../../RENDERING.md)에 있다. [분석 스크립트](../../tools/Analyze-RenderLog.ps1)는 CSV를 읽어 첫 프레임 비용, 전체 생성/로드/업로드 합계, warm 평균 및 nearest-rank p50/p95를 JSON으로 출력한다. 기본 첫 프레임을 제외하며 -SkipWarmupFrames로 조정한다. 이동·전투·보스·리사이즈 장기 측정을 같은 방식으로 보관하면 구조 변경 전후 비교 기준으로 쓸 수 있다.

## 6. 검증과 다음 개선 순서

회귀 검사는 배칭/분리 제출 픽셀 일치, 캐시 변환/투명 순서, descriptor/atlas-page 분할, 파일 손상/버전/길이/체크섬, 재로드 시 builder 미실행, LRU, GL 오류, HDR/후처리/해상도, 기존 전투/성장/맵 연결성을 포함한다. 스모크 테스트는 성능 벤치마크가 아니다.

최종 검증: x64 Debug/Release 빌드 및 각 --smoke-test 종료 코드 0. 128개 시드/1152개 청크 연결성 검사 통과, GL 오류 0, 기본 캡처 11회/후처리 비활성 캡처 3회. 분석 스크립트 cold/warm CSV 실행 및 clang-format 검사 통과.

1. GPU timer query로 scene/post/UI GPU 시간을 계측한다. RenderCPU는 GPU 시간 대체값이 아니다.
2. 첫 방문 히치: 메시 사전 bake, 비동기 파일 I/O/생성, render-thread 업로드 예산. 현재 파일 I/O는 동기식이다.
3. 아틀라스 증분 할당: 현재 메시 유입/퇴거 시 전체 resident 아틀라스를 재업로드한다. 페이지 allocator와 부분 업로드로 제한한다.
4. 남는 작은 도형/발광 overdraw를 계측해 culling/LOD를 개선한다. 드로 콜이 적어도 삼각형·프래그먼트 작업은 남는다.
5. 인스턴스 스트림 ring buffer를 검토한다. TBO fetch와 삼각형 참조 생성 비용 때문에 다른 GPU에서는 conventional instancing과 비교가 필요하다.
6. 후처리 GPU 시간이 높을 경우 블러 해상도/탭 수/알고리즘을 조정한다. 호출 수만 줄이려고 화질을 바꾸지 않는다.
7. 장시간 이동, 다양한 시드/저사양 GPU, 캐시 쓰기 불가, 대량 삼각형 경계 스트레스 실험을 확장한다. 로그 보존/압축과 자동 비교 임계값도 후속 과제다.

변경 파일: PrototypeRenderer, Scene.vs, MeshDiskCache, FrameProfiler, RenderDiagnostics, SimpleGame.cpp. Actor/SceneGraph의 게임 로직과 그리기 순서는 유지한다.
