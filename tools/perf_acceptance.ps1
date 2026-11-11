<#
.SYNOPSIS
    Performance acceptance on the owner notebook (docs/PERFORMANCE.md, roadmap M7-04).

.DESCRIPTION
    Generates the test logs (once, cached), runs lv_bench, and prints a table of the budgets
    P-IDX-1..3 and P-SRCH-1..5 with PASS/FAIL. The table is also written as Markdown so it can
    be pasted into the results log of docs/PERFORMANCE.md.

    Run it from the unpacked "LogViewer-perf-..." CI artifact (lv_bench.exe next to this script;
    lv_bench generates the test logs itself) or pass -Bench. Close other programs first; the run takes a few minutes and
    needs about 2.5 GB of free disk space for the large and short profiles.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\perf_acceptance.ps1
.EXAMPLE
    .\perf_acceptance.ps1 -Profiles small -Repetitions 1   # quick check
#>
[CmdletBinding()]
param(
    [string]$Bench = (Join-Path $PSScriptRoot 'lv_bench.exe'),
    [string]$Data = (Join-Path $env:LOCALAPPDATA 'LogViewer\bench-data'),
    [string[]]$Profiles = @('small', 'large', 'short'),
    [int]$Repetitions = 3,
    [string]$Out = (Join-Path $PSScriptRoot 'perf-results.md')
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Bench)) { throw "lv_bench not found: $Bench (use -Bench)" }
New-Item -ItemType Directory -Force $Data | Out-Null

$env:LV_BENCH_DATA = $Data
$env:LV_BENCH_PROFILES = ($Profiles -join ',')
$json = Join-Path $Data 'perf-acceptance.json'
$filter = 'BM_(ReadOnly_Sequential|Index|Search_Literal|Search_Literal_1T|Search_Regex|Search_Boolean)/'

Write-Host "Running $Bench on $($Profiles -join ', ') ($Repetitions repetitions); data in $Data"
& $Bench "--benchmark_filter=$filter" "--benchmark_repetitions=$Repetitions" `
    '--benchmark_report_aggregates_only=true' "--benchmark_out=$json" '--benchmark_out_format=json'
if ($LASTEXITCODE -ne 0) { throw "lv_bench failed with exit code $LASTEXITCODE" }

$result = Get-Content $json -Raw | ConvertFrom-Json
# Median of each benchmark (aggregate "median"); fall back to the single run.
$runs = @{}
foreach ($b in $result.benchmarks) {
    $name = $b.run_name
    if (-not $name) { $name = $b.name }
    $name = $name -replace '/real_time$', ''  # UseRealTime() appends this suffix
    if ($b.run_type -eq 'aggregate' -and $b.aggregate_name -ne 'median') { continue }
    $runs[$name] = $b
}
function Get-Run([string]$name) {
    if ($runs.ContainsKey($name)) { return $runs[$name] }
    return $null
}
function Get-Ms($run) {
    if (-not $run) { return $null }
    $factor = switch ($run.time_unit) { 'ns' { 1e-6 } 'us' { 1e-3 } 'ms' { 1 } 's' { 1000 } default { 1 } }
    return [double]$run.real_time * $factor
}
function Get-GiBs($run) {
    if (-not $run -or -not $run.bytes_per_second) { return $null }
    return [double]$run.bytes_per_second / 1GB
}

$rows = [System.Collections.Generic.List[object]]::new()
function Add-Row([string]$id, [string]$scenario, [string]$budget, $value, [string]$unit, [scriptblock]$pass) {
    if ($null -eq $value) {
        $rows.Add([pscustomobject]@{ ID = $id; Scenario = $scenario; Budget = $budget; Result = 'n/a'; Status = 'SKIP' })
        return
    }
    $ok = & $pass $value
    $rows.Add([pscustomobject]@{
            ID = $id; Scenario = $scenario; Budget = $budget
            Result = ('{0:N2} {1}' -f $value, $unit); Status = $(if ($ok) { 'PASS' } else { 'FAIL' })
        })
}

$indexSmall = Get-Run 'BM_Index/small'
Add-Row 'P-IDX-1' 'Full index of small' '<= 150 ms' (Get-Ms $indexSmall) 'ms' { param($v) $v -le 150 }
foreach ($p in 'large', 'short') {
    $index = Get-Run "BM_Index/$p"
    $read = Get-Run "BM_ReadOnly_Sequential/$p"
    $speed = Get-GiBs $index
    $readSpeed = Get-GiBs $read
    # >= 2 GB/s, or >= 90% of a plain read loop, whichever is lower.
    $target = 2e9 / 1GB
    if ($readSpeed) { $target = [Math]::Min($target, 0.9 * $readSpeed) }
    Add-Row 'P-IDX-2' "Index throughput on $p (read loop: $('{0:N2}' -f $readSpeed) GiB/s)" ('>= {0:N2} GiB/s' -f $target) $speed 'GiB/s' { param($v) $v -ge $target }
    $limit = if ($p -eq 'short') { 3 } else { 2 }
    $perLine = if ($index) { [double]$index.index_bytes_per_line } else { $null }
    Add-Row 'P-IDX-3' "Index memory on $p" "<= $limit B/line" $perLine 'B/line' { param($v) $v -le $limit }
}
Add-Row 'P-SRCH-1' 'Literal "ERROR" on small' '<= 100 ms' (Get-Ms (Get-Run 'BM_Search_Literal/small')) 'ms' { param($v) $v -le 100 }
Add-Row 'P-SRCH-2' 'Regex /(ERROR|WARN).*timeout/ on large' '<= 1000 ms' (Get-Ms (Get-Run 'BM_Search_Regex/large')) 'ms' { param($v) $v -le 1000 }
Add-Row 'P-SRCH-3' 'Boolean /ERROR/ AND NOT "heartbeat" on large' '<= 1200 ms' (Get-Ms (Get-Run 'BM_Search_Boolean/large')) 'ms' { param($v) $v -le 1200 }
$all = Get-Run 'BM_Search_Literal/large'
$one = Get-Run 'BM_Search_Literal_1T/large'
$speedup = if ($all -and $one) { (Get-Ms $one) / (Get-Ms $all) } else { $null }
$threads = if ($all) { [int]$all.threads } else { 0 }
# The budget assumes 4 threads; with fewer cores the ideal speed-up is lower.
$wanted = if ($threads -ge 4 -or $threads -eq 0) { 3.0 } else { 0.75 * $threads }
Add-Row 'P-SRCH-4' "Literal on large, $threads threads vs. 1" ('>= {0:N1}x' -f $wanted) $speedup 'x' { param($v) $v -ge $wanted }
$listBytes = if ($all -and $all.list_bytes_per_match) { [double]$all.list_bytes_per_match } else { $null }
Add-Row 'P-SRCH-5' 'Match list memory (large, literal)' '<= 2 B/match' $listBytes 'B/match' { param($v) $v -le 2 }

$cpu = (Get-CimInstance Win32_Processor | Select-Object -First 1).Name.Trim()
$ram = '{0:N0} GB' -f ((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
$disk = (Get-PhysicalDisk | Where-Object { $_.DeviceId -eq '0' } | Select-Object -First 1).FriendlyName
$machine = "$cpu, $ram RAM, $disk"

Write-Host ''
Write-Host "Machine: $machine"
$rows | Format-Table -AutoSize | Out-String | Write-Host

$date = Get-Date -Format 'yyyy-MM-dd'
$md = @("# Performance acceptance ($date)", '', "Machine: $machine", '',
    '| ID | Scenario | Budget | Result | Status |', '| --- | --- | --- | --- | --- |')
foreach ($r in $rows) { $md += "| $($r.ID) | $($r.Scenario) | $($r.Budget) | $($r.Result) | $($r.Status) |" }
$md += @('', 'Manual checks (not measured by this script): P-OPEN-1, P-UI-1/2, P-TAIL-1/2, P-MEM-1,',
    'P-START-1. For the klogg comparison, open the same files from the data folder in klogg and note',
    'the time to the first screen and to a finished filter for "ERROR" on large.')
$md | Set-Content -Encoding utf8 $Out
Write-Host "Results written to $Out"

$failed = @($rows | Where-Object Status -eq 'FAIL').Count
if ($failed -gt 0) { Write-Warning "$failed budget(s) not met" }
exit 0
