param([switch]$RuntimeChecks)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$command = Get-Command MSBuild.exe -ErrorAction SilentlyContinue
$builder = if ($command) { $command.Source } else { $null }
if (-not $builder) {
    $locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $locator) {
        $builder = & $locator -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild/**/Bin/MSBuild.exe' | Select-Object -First 1
    }
}
if (-not $builder) { throw 'Visual Studio 2022 C++ Desktop 도구와 Windows SDK가 필요합니다.' }
foreach ($platform in @('x64','x86')) {
    foreach ($configuration in @('Release','Debug')) {
        $output = Join-Path $projectRoot "x64/GlfwMatrix/$platform/$configuration/"
        & $builder (Join-Path $projectRoot 'SimpleGame.sln') /t:Rebuild "/p:Configuration=$configuration" "/p:Platform=$platform" "/p:GlfwMatrixOutput=$output" /v:quiet /nologo
        if ($LASTEXITCODE -ne 0) { throw "FAIL build $configuration $platform" }
        Write-Output "PASS build $configuration $platform"
        if ($RuntimeChecks) {
            & (Join-Path $output 'SimpleGame.exe') --platform-test --seed=2026 --no-monitor
            if ($LASTEXITCODE -ne 0) { throw "FAIL runtime $configuration $platform" }
            Write-Output "PASS runtime $configuration $platform"
        }
    }
}
