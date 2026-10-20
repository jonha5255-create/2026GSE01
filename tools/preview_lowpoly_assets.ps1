param([switch]$Validate)
$ErrorActionPreference = 'Stop'
$assetProject = Split-Path $PSScriptRoot -Parent
$assetExecutable = Join-Path $assetProject 'x64/Release/SimpleGame.exe'
if (-not (Test-Path -LiteralPath $assetExecutable)) { throw '먼저 Release x64로 빌드하세요.' }
Push-Location $assetProject
try {
    if ($Validate) { & $assetExecutable --asset-test }
    else { & $assetExecutable --asset-preview --no-monitor }
    if ($LASTEXITCODE -ne 0) { throw "에셋 실행 실패: 종료 코드 $LASTEXITCODE" }
} finally { Pop-Location }
