# Developer-only temporary subscriptions. No display, firmware or ACL changes.
param([int]$Seconds = 180)
$ErrorActionPreference = 'Stop'
$taskTag = Get-Date -Format 'yyyyMMdd-HHmmss'
$taskLog = Join-Path $PSScriptRoot "artifacts\native-key-vendor-$taskTag-$PID.txt"
$taskJobs = @()
"START $(Get-Date -Format o) seconds=$Seconds" | Set-Content -LiteralPath $taskLog
try {
    foreach ($taskClass in @('HID_EVENT20', 'HID_EVENT21', 'HID_EVENT22', 'HID_EVENT23', 'WmiMonitorBrightnessEvent')) {
        $taskJobs += Register-CimIndicationEvent -Namespace root/wmi -Query "SELECT * FROM $taskClass" -SourceIdentifier "ScreenSave.$taskTag.$taskClass" -MessageData $taskLog -Action {
            $taskEvent = $Event.SourceEventArgs.NewEvent
            $taskClassName = $taskEvent.CimClass.CimClassName
            $taskValues = if ($taskClassName -eq 'WmiMonitorBrightnessEvent') { "brightness=$($taskEvent.Brightness)" } else { "EventDetail=$($taskEvent.EventDetail -join ',')" }
            "$(Get-Date -Format o) $taskClassName $taskValues" | Add-Content -LiteralPath $Event.MessageData
        }
        "READY $taskClass" | Add-Content -LiteralPath $taskLog
    }
    "READY all subscriptions; log=$taskLog"
    $taskEnd = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $taskEnd) { Wait-Event -Timeout 5 | Out-Null }
} finally {
    foreach ($taskJob in $taskJobs) {
        Unregister-Event -SourceIdentifier $taskJob.Name
        Remove-Job -Job $taskJob -Force
    }
    "STOPPED $(Get-Date -Format o)" | Add-Content -LiteralPath $taskLog
}
