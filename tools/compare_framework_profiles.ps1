param(
    [Parameter(Mandatory=$true)][string]$BeforeLogs,
    [Parameter(Mandatory=$true)][string]$AfterLogs,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../doc/validation/glfw-migration'),
    [int]$Runs = 3,
    [int]$WarmupFrames = 30
)
$ErrorActionPreference = 'Stop'
$culture = [System.Globalization.CultureInfo]::InvariantCulture
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
function Value($text) { return [double]::Parse([string]$text, $culture) }
function Mean($values) { return ($values | Measure-Object -Average).Average }
function P95($values) {
    $ordered = @($values | Sort-Object)
    return $ordered[[Math]::Max(0, [Math]::Ceiling($ordered.Count * .95) - 1)]
}
$summaries = @()
foreach ($side in @(@('FreeGLUT',$BeforeLogs),@('GLFW',$AfterLogs))) {
    $files = @(Get-ChildItem -LiteralPath $side[1] -Filter '*.jsonl' | Sort-Object LastWriteTime -Descending | Where-Object {
        $sessionLine = Get-Content -LiteralPath $_.FullName -TotalCount 1
        $sessionLine -match 'mode=profile-test'
    } | Select-Object -First $Runs)
    if ($files.Count -ne $Runs) { throw "Need $Runs profile logs for $($side[0])" }
    $run = 0
    foreach ($file in $files) {
        ++$run
        $csv = [System.IO.Path]::ChangeExtension($file.FullName, '.csv')
        $rows = @(Import-Csv -LiteralPath $csv | Where-Object { [int]$_.frame -gt $WarmupFrames })
        if ($rows.Count -lt 100) { throw "Insufficient measured frames: $csv" }
        $frames = @($rows | ForEach-Object { Value $_.frame_ms })
        $summary = [pscustomobject]@{
            framework=$side[0]; run=$run; measured_frames=$rows.Count
            fps=1000/(Mean $frames); frame_mean_ms=Mean $frames; frame_p95_ms=P95 $frames
            render_mean_ms=Mean @($rows | ForEach-Object { Value $_.render_cpu_ms })
            render_p95_ms=P95 @($rows | ForEach-Object { Value $_.render_cpu_ms })
            update_mean_ms=Mean @($rows | ForEach-Object { Value $_.update_cpu_ms })
            draw_calls=Mean @($rows | ForEach-Object { Value $_.draw_calls })
            triangle_instances=Mean @($rows | ForEach-Object { Value $_.triangle_instances })
            mesh_generations=($rows | Measure-Object -Property mesh_generations -Sum).Sum
        }
        $summaries += $summary
        Copy-Item -LiteralPath $csv -Destination (Join-Path $OutputDirectory "$($side[0])-run$run.csv")
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $OutputDirectory "$($side[0])-run$run.jsonl")
    }
}
$summaries | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'summary.csv') -NoTypeInformation -Encoding utf8
$summaries | Format-Table framework,run,fps,frame_mean_ms,frame_p95_ms,render_mean_ms,update_mean_ms,draw_calls,mesh_generations
foreach ($framework in @('FreeGLUT','GLFW')) {
    $rows = @($summaries | Where-Object framework -eq $framework)
    Write-Output ("{0}: FPS={1:N2} Frame={2:N3}ms P95={3:N3}ms Render={4:N3}ms Update={5:N3}ms" -f $framework,
        (Mean @($rows.fps)), (Mean @($rows.frame_mean_ms)), (Mean @($rows.frame_p95_ms)),
        (Mean @($rows.render_mean_ms)), (Mean @($rows.update_mean_ms)))
}
