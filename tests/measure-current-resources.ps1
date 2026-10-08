$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)
$outputDirectory = Join-Path $PSScriptRoot 'artifacts/current-resources'
[void](New-Item -ItemType Directory -Force -Path $outputDirectory)
$probe = (Resolve-Path 'build/app-probe.exe').Path
$initialState = & $probe 0
if ($initialState -ne 'matrix=1.000 gamma_read=1 gamma_mid=16448 gamma_white=32767 execution=3') {
    throw "This measurement expects the observed initial Gamma-dark state: $initialState"
}
$initialBrightness = (Get-CimInstance -Namespace root/wmi -ClassName WmiMonitorBrightness).CurrentBrightness
$app = Get-Process ScreenSave
$dwmId = (Get-Process dwm).Id
$isDark = $true
$rows = @()
$raw = @()
$paths = @(
    '\Process(ScreenSave)\ID Process',
    '\Process(ScreenSave)\Working Set - Private',
    '\Process(ScreenSave)\Private Bytes',
    '\Process(ScreenSave)\Working Set',
    '\Process(dwm)\% Processor Time',
    '\Process(dwm)\Working Set - Private',
    '\GPU Engine(*)\Utilization Percentage'
)
function Mean($samples, $suffix) {
    ($samples | Where-Object { $_.Path.EndsWith($suffix) } | Measure-Object CookedValue -Average).Average
}
try {
    $sequence = @('normal','matrix','gamma', 'gamma','normal','matrix', 'matrix','gamma','normal')
    for ($i = 0; $i -lt $sequence.Count; ++$i) {
        $mode = $sequence[$i]
        if ($mode -eq 'normal') {
            if ($isDark) { & $probe 5 | Out-Null; $isDark = $false }
        } else {
            $command = if ($mode -eq 'matrix') { 3 } else { 4 }
            & $probe $command | Out-Null
            if (!$isDark) { & $probe 5 | Out-Null; $isDark = $true }
        }
        Start-Sleep -Seconds 2
        $state = & $probe 0
        $app.Refresh()
        $cpuStart = $app.TotalProcessorTime.TotalSeconds
        $started = Get-Date
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $samples = Get-Counter -Counter $paths -SampleInterval 1 -MaxSamples 15
        $timer.Stop()
        $app.Refresh()
        $cpuSeconds = $app.TotalProcessorTime.TotalSeconds - $cpuStart
        $all = @($samples.CounterSamples)
        $processIds = @($all | Where-Object { $_.Path.EndsWith('\process(screensave)\id process') } | Select-Object -ExpandProperty CookedValue -Unique)
        if ($processIds.Count -ne 1 -or $processIds[0] -ne $app.Id) { throw 'App process changed during measurement.' }
        $gpu = @($all | Where-Object InstanceName -Like "pid_$($dwmId)_*" | Group-Object Timestamp | ForEach-Object { ($_.Group | Measure-Object CookedValue -Sum).Sum })
        $row = [ordered]@{
            mode = $mode
            round = [int][math]::Floor($i / 3) + 1
            started = $started.ToString('o')
            seconds = $timer.Elapsed.TotalSeconds
            state = $state
            app_cpu_ms = $cpuSeconds * 1000
            app_cpu_one_core_pct = 100 * $cpuSeconds / $timer.Elapsed.TotalSeconds
            app_private_working_bytes = Mean $all '\process(screensave)\working set - private'
            app_private_commit_bytes = Mean $all '\process(screensave)\private bytes'
            app_working_bytes = Mean $all '\process(screensave)\working set'
            app_threads_end = $app.Threads.Count
            app_handles_end = $app.HandleCount
            dwm_cpu_one_core_pct = Mean $all '\process(dwm)\% processor time'
            dwm_private_working_bytes = Mean $all '\process(dwm)\working set - private'
            dwm_gpu_engine_sum_pct = ($gpu | Measure-Object -Average).Average
        }
        $rows += $row
        $raw += [ordered]@{ mode = $mode; round = $row.round; counters = @($all | Select-Object Timestamp,Path,CookedValue,Status) }
        $rows | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $outputDirectory 'rounds.json')
        $row | ConvertTo-Json -Compress
    }
} finally {
    # Return to the Gamma-dark state that was active before measurement.
    & $probe 4 | Out-Null
    if (!$isDark) { & $probe 5 | Out-Null; $isDark = $true }
    $finalState = & $probe 0
    $metadata = [ordered]@{
        measuredAt = (Get-Date).ToString('o')
        processId = $app.Id
        exe = (Get-FileHash 'dist/ScreenSave.exe').Hash
        exeBytes = (Get-Item 'dist/ScreenSave.exe').Length
        initialState = $initialState
        finalState = $finalState
        initialBrightness = $initialBrightness
        finalBrightness = (Get-CimInstance -Namespace root/wmi -ClassName WmiMonitorBrightness).CurrentBrightness
        completedRounds = $rows.Count
        workload = 'Existing desktop and applications; no fixed browser workload, no app-off baseline. Switching and two-second settling excluded from CPU sample.'
    }
    $metadata | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $outputDirectory 'metadata.json')
    $raw | ConvertTo-Json -Depth 7 | Set-Content -Encoding utf8 (Join-Path $outputDirectory 'counters.json')
    $metadata | ConvertTo-Json -Compress
}
