param(
    [Parameter(Mandatory = $true)][string]$Csv,
    [ValidateRange(0, 10000000)][int]$SkipWarmupFrames = 1
)

$ErrorActionPreference = 'Stop'
$rows = @(Import-Csv -LiteralPath $Csv)
if ($rows.Count -le $SkipWarmupFrames) { throw 'Not enough frames after warmup exclusion.' }
$culture = [Globalization.CultureInfo]::InvariantCulture
function Number($value) { [double]::Parse($value, $culture) }
function Sum($items, $column) {
    $total = 0.0
    foreach ($item in $items) { $total += Number $item.$column }
    return $total
}
function Stats($items, $column) {
    $values = @($items | ForEach-Object { Number $_.$column } | Sort-Object)
    [ordered]@{
        mean = (Sum $items $column) / $values.Count
        min = $values[0]
        p50 = $values[[Math]::Max(0, [Math]::Ceiling($values.Count * 0.50) - 1)]
        p95 = $values[[Math]::Max(0, [Math]::Ceiling($values.Count * 0.95) - 1)]
        max = $values[-1]
    }
}
$warm = @($rows | Select-Object -Skip $SkipWarmupFrames)
$interval = Sum $rows 'frame_ms'
[ordered]@{
    source = [IO.Path]::GetFileName($Csv)
    frames = $rows.Count
    excluded_warmup_frames = $SkipWarmupFrames
    fps_all = $(if ($interval -gt 0) { 1000 * $rows.Count / $interval } else { 0 })
    first_frame_ms = Number $rows[0].frame_ms
    first_render_cpu_ms = Number $rows[0].render_cpu_ms
    warm_frame_ms = Stats $warm 'frame_ms'
    warm_render_cpu_ms = Stats $warm 'render_cpu_ms'
    draw_calls = Stats $rows 'draw_calls'
    mesh_generations_all = Sum $rows 'mesh_generations'
    disk_loads_all = Sum $rows 'disk_loads'
    disk_rejects_all = Sum $rows 'disk_rejects'
    mesh_upload_bytes_all = Sum $rows 'mesh_upload_bytes'
    warm_mesh_generations = Sum $warm 'mesh_generations'
    warm_mesh_upload_bytes = Sum $warm 'mesh_upload_bytes'
    warm_instance_upload_bytes = Stats $warm 'instance_upload_bytes'
    batch_splits_all = Sum $rows 'batch_splits'
} | ConvertTo-Json -Depth 5
