# C++ 코드 컨벤션

줄바꿈을 충분히 사용하는 Allman 스타일을 적용합니다.
자동 서식의 기준은 저장소 루트의 `.clang-format`이며 clang-format 19.1.5에서 확인합니다.

- 들여쓰기는 공백 4칸을 사용합니다.
- 함수, 클래스, 조건문, 반복문의 여는 중괄호는 다음 줄에 씁니다.
- 한 줄에 여러 실행 문장을 넣지 않습니다.
- 짧은 조건문과 반복문에도 중괄호를 사용합니다.
- 짧은 함수와 람다도 본문을 여러 줄로 풉니다.
- 함수 정의 사이에는 빈 줄 하나를 둡니다.
- 긴 식과 호출은 100열을 기준으로 나눕니다.
- 포인터와 참조는 `Type*`, `Type&` 형식을 사용합니다.
- include 순서를 유지합니다. 특히 미리 컴파일된 헤더 `stdafx.h`가 먼저 와야 합니다.
- 외부 라이브러리 `Dependencies`의 코드는 서식 변경 대상에서 제외합니다.
- 내장 GLSL은 `R"glsl(...)glsl"` 구분자를 사용하며 Microsoft/Allman 서식을 적용합니다.
  대사 등 다른 원시 문자열에는 이 서식을 적용하지 않습니다.

```cpp
void Update(float deltaTime)
{
    if (deltaTime <= 0.0f)
    {
        return;
    }

    UpdatePlayer(deltaTime);
    UpdateCamera(deltaTime);
}
```

Visual Studio의 문서 서식 명령에서 `.clang-format`을 사용하도록 설정하거나,
프로젝트 루트에서 다음 PowerShell 명령을 실행합니다.

```powershell
$formatter = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\clang-format.exe'
$sources = Get-ChildItem -LiteralPath SimpleGame -File |
    Where-Object { $_.Extension -in '.cpp', '.h' } |
    ForEach-Object { $_.FullName }
& $formatter -i --style=file $sources
& $formatter --dry-run --Werror --style=file $sources
& $formatter -i --style=file --assume-filename=shader.cpp SimpleGame/Shaders/SolidRect.vs SimpleGame/Shaders/SolidRect.fs
```

소스 재귀 검색을 사용하지 않아 외부 헤더가 변경되지 않도록 합니다.
서식 변경 후 Debug/Release 빌드와 `--smoke-test`로 실행을 확인합니다.
