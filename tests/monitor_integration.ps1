param(
    [string]$GamePath = (Join-Path $PSScriptRoot '..\x64\Release\SimpleGame.exe'),
    [switch]$Legacy
)

$ErrorActionPreference = 'Stop'
$gameExecutable = (Resolve-Path -LiteralPath $GamePath).Path
$portProbe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$portProbe.Start()
$monitorTestPort = $portProbe.LocalEndpoint.Port
$portProbe.Stop()
$monitorUrl = "http://127.0.0.1:$monitorTestPort"
$gameArgs = @('--monitor-test', "--monitor-port=$monitorTestPort", '--seed=2026')
if ($Legacy) { $gameArgs += '--level-one' }
$monitorLogDirectory = Join-Path (Split-Path $gameExecutable) 'Logs'
$null = New-Item -ItemType Directory -Path $monitorLogDirectory -Force
$monitorRunId = [guid]::NewGuid().ToString('N')
$monitorStdout = Join-Path $monitorLogDirectory "monitor-test-$monitorRunId.stdout.log"
$monitorStderr = Join-Path $monitorLogDirectory "monitor-test-$monitorRunId.stderr.log"
$gameProcess = Start-Process -FilePath $gameExecutable -ArgumentList $gameArgs -WorkingDirectory (Split-Path $gameExecutable) -WindowStyle Hidden -RedirectStandardOutput $monitorStdout -RedirectStandardError $monitorStderr -PassThru

function Check([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "FAIL $Message" }
    Write-Output "PASS $Message"
}

try {
    $first = $null
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        if ($gameProcess.HasExited) { throw "Game exited before HTTP state became ready: $($gameProcess.ExitCode)" }
        try {
            $candidate = Invoke-RestMethod -Uri "$monitorUrl/api/state" -TimeoutSec 1
            if ($candidate.type -eq 'world_snapshot') { $first = $candidate; break }
        } catch { }
        Start-Sleep -Milliseconds 100
    }
    Check ($null -ne $first) 'game publishes JSON over loopback HTTP'
    Check ($null -ne $first.incidents -and $null -ne $first.incidentTotals -and $first.incidentTotals.murder -ge 0 -and $first.incidentTotals.rumor -ge 0) 'JSON includes incident history and murder rumor totals'
    Check ($first.schemaVersion -eq 1 -and $first.sequence -ge 1 -and $first.sessionId.Length -gt 0) 'message schema and sequence metadata'
    Check ($first.counts.npcs -eq @($first.npcs).Count -and $first.counts.monsters -eq @($first.monsters).Count) 'entity arrays match reported population'
    if ($Legacy) {
        Check ($first.mode -eq 'level-one' -and @($first.villages).Count -eq 0 -and $first.counts.npcs -eq 0) 'legacy mode does not fabricate villages or NPCs'
    } else {
        Check ($first.counts.npcs -eq 210 -and $first.counts.monsters -ge 51 -and @($first.villages).Count -eq 7) 'world includes seven persistent villages and 210 residents'
        Check ($first.villages[0].name -eq '잔명 마을') 'HTTP delivers Korean UTF-8 names'
        Check ($first.villages[0].counts.npcs -eq 30 -and $first.villages[0].counts.monsters -eq 3) 'village population aggregation'
        Check (@($first.villages | Where-Object { $_.counts.npcs -ne 30 -or @($_.homes).Count -ne 36 }).Count -eq 0) 'every village exposes 30 residents and 36 house slots'
        Check (@($first.npcs | Where-Object { $_.homeSlot -lt 0 }).Count -eq 0 -and @($first.npcs | Where-Object courier).Count -eq 7) 'every resident has a home and each village has a courier'
        $goldSum = ($first.npcs | Measure-Object -Property gold -Sum).Sum
        $villageGoldSum = ($first.villages | Measure-Object -Property totalGold -Sum).Sum
        Check ($villageGoldSum -eq $goldSum -and $first.economy.npcGold -eq $goldSum) 'village and world economy match individual NPC assets'
    }
    $dashboardResponse = Invoke-WebRequest -Uri "$monitorUrl/" -UseBasicParsing -TimeoutSec 2
    Check ($dashboardResponse.StatusCode -eq 200 -and $dashboardResponse.Content -match '라이브 모니터') 'local HTML dashboard is served'
    Check ($dashboardResponse.Content -match '사건 모니터링' -and $dashboardResponse.Content -match 'incident-filter') 'dashboard exposes incident monitoring and type filter'
    foreach ($asset in @('monitor.js', 'monitor.css')) {
        $assetResponse = Invoke-WebRequest -Uri "$monitorUrl/$asset" -UseBasicParsing -TimeoutSec 2
        Check ($assetResponse.StatusCode -eq 200 -and $assetResponse.RawContentLength -gt 100) "dashboard asset $asset is available"
    }
    Start-Sleep -Milliseconds 650
    $next = Invoke-RestMethod -Uri "$monitorUrl/api/state" -TimeoutSec 2
    Check ($next.sequence -gt $first.sequence -and $next.generatedAtUnixMs -gt $first.generatedAtUnixMs) 'state messages advance while game runs'
    $missingStatus = 0
    try { Invoke-WebRequest -Uri "$monitorUrl/not-a-route" -UseBasicParsing -TimeoutSec 2 | Out-Null }
    catch { if ($_.Exception.Response) { $missingStatus = [int]$_.Exception.Response.StatusCode } }
    Check ($missingStatus -eq 404) 'unknown routes return HTTP 404'
    $methodStatus = 0
    try { Invoke-WebRequest -Uri "$monitorUrl/api/state" -Method Post -UseBasicParsing -TimeoutSec 2 | Out-Null }
    catch { if ($_.Exception.Response) { $methodStatus = [int]$_.Exception.Response.StatusCode } }
    Check ($methodStatus -eq 405) 'monitor API accepts read-only GET requests'
    if (-not $gameProcess.WaitForExit(15000)) { throw "Monitor test game did not exit cleanly. Logs: $monitorStdout" }
    Check ($gameProcess.ExitCode -eq 0) 'game and monitoring worker shut down cleanly'
    $stopped = $false
    try { Invoke-RestMethod -Uri "$monitorUrl/api/state" -TimeoutSec 1 | Out-Null }
    catch { $stopped = $true }
    Check $stopped 'loopback server closes when game exits'
} finally {
    if (-not $gameProcess.HasExited) {
        $null = $gameProcess.CloseMainWindow()
        if (-not $gameProcess.WaitForExit(2000)) { $gameProcess.Kill() }
    }
    $gameProcess.Dispose()
}
