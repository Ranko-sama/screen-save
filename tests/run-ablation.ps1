# Developer-only sequential experiments; production never runs this script.
param([string[]] $Names)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content (Join-Path $projectRoot 'build\review\ablation\manifest.json') -Raw | ConvertFrom-Json
if ($Names) { $manifest = @($manifest | Where-Object name -In $Names) }
$artifactDirectory = Join-Path $PSScriptRoot 'artifacts\ablation'
New-Item -ItemType Directory -Path $artifactDirectory -Force | Out-Null
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class AblationDisplay {
 [StructLayout(LayoutKind.Sequential)] public struct Point { public int x,y; }
 [DllImport("Magnification.dll")] public static extern bool MagInitialize();
 [DllImport("Magnification.dll")] public static extern bool MagUninitialize();
 [DllImport("Magnification.dll")] static extern bool MagGetFullscreenColorEffect([Out] float[] matrix);
 [DllImport("Magnification.dll")] static extern bool MagSetFullscreenColorEffect(float[] matrix);
 [DllImport("Magnification.dll")] static extern bool MagShowSystemCursor(bool show);
 [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr window);
 [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr window,IntPtr dc);
 [DllImport("gdi32.dll")] static extern bool GetDeviceGammaRamp(IntPtr dc,[Out] ushort[] ramp);
 [DllImport("gdi32.dll")] static extern bool SetDeviceGammaRamp(IntPtr dc,ushort[] ramp);
 [DllImport("user32.dll")] static extern bool GetPhysicalCursorPos(out Point point);
 [DllImport("user32.dll")] static extern bool SetPhysicalCursorPos(int x,int y);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window,uint message,IntPtr w,IntPtr l);
 public static float[] matrix=new float[25];
 public static ushort[] ramp=new ushort[768];
 public static void Capture() { MagInitialize(); MagGetFullscreenColorEffect(matrix);var dc=GetDC(IntPtr.Zero);GetDeviceGammaRamp(dc,ramp);ReleaseDC(IntPtr.Zero,dc); }
 public static void Restore() { MagSetFullscreenColorEffect(matrix);var dc=GetDC(IntPtr.Zero);SetDeviceGammaRamp(dc,ramp);ReleaseDC(IntPtr.Zero,dc);MagShowSystemCursor(true);Point p;GetPhysicalCursorPos(out p);SetPhysicalCursorPos(p.x,p.y); }
}
'@
if ([AblationDisplay]::FindWindow('ScreenSave','Screen Save') -ne [IntPtr]::Zero) {
    throw 'Close Screen Save before running global display experiments.'
}
[AblationDisplay]::Capture()
$savedBrightness = [byte](Get-CimInstance -Namespace root/wmi -ClassName WmiMonitorBrightness | Where-Object Active | Select-Object -First 1 -ExpandProperty CurrentBrightness)
$brightnessControl = Get-CimInstance -Namespace root/wmi -ClassName WmiMonitorBrightnessMethods | Where-Object Active | Select-Object -First 1
$results = [Collections.Generic.List[object]]::new()
if ($Names -and (Test-Path -LiteralPath (Join-Path $artifactDirectory 'results.json'))) {
    Get-Content (Join-Path $artifactDirectory 'results.json') -Raw | ConvertFrom-Json |
        Where-Object name -NotIn $Names | ForEach-Object { $results.Add($_) }
}
try {
    foreach ($candidate in $manifest) {
        $log = Join-Path $artifactDirectory ($candidate.name + '.txt')
        $started = Get-Date
        try {
            if ($candidate.test -eq 'cursor') {
                $child = Start-Process -FilePath $candidate.executable -WindowStyle Hidden -PassThru
                Start-Sleep -Milliseconds 800
                & (Join-Path $projectRoot 'build\review\cursor-probe.exe') app *> $log
                $resultCode = $LASTEXITCODE
                if (!$child.WaitForExit(5000)) { throw "Candidate did not exit: $($candidate.name)" }
            } else {
                $probe = switch ($candidate.test) {
                    'review' { 'review-probe.exe' }
                    'menu' { 'menu-hotkey-probe.exe' }
                    default { 'toggle-probe.exe' }
                }
                & (Join-Path $projectRoot ('build\review\' + $probe)) $candidate.executable *> $log
                $resultCode = $LASTEXITCODE
            }
        } finally {
            $window = [AblationDisplay]::FindWindow('ScreenSave','Screen Save')
            if ($window -ne [IntPtr]::Zero) {
                [void][AblationDisplay]::PostMessage($window,0x0010,[IntPtr]::Zero,[IntPtr]::Zero)
                Start-Sleep -Milliseconds 800
            }
            [AblationDisplay]::Restore()
            $brightnessControl | Invoke-CimMethod -MethodName WmiSetBrightness -Arguments @{Timeout=[uint32]0;Brightness=$savedBrightness} | Out-Null
            Start-Sleep -Milliseconds 350
        }
        $failedChecks = @(Select-String -LiteralPath $log -Pattern '^FAIL ' | ForEach-Object Line)
        $expectationMatched = if ($null -eq $candidate.expectedFailure) { $null } else { ($resultCode -ne 0) -eq $candidate.expectedFailure }
        $row = [pscustomobject]@{
            name=$candidate.name; base=$candidate.base; test=$candidate.test
            expectedFailure=$candidate.expectedFailure; exitCode=$resultCode
            expectationMatched=$expectationMatched; failedChecks=$failedChecks
            seconds=[math]::Round(((Get-Date)-$started).TotalSeconds,2)
            bytes=(Get-Item -LiteralPath $candidate.executable).Length
            sha256=(Get-FileHash -LiteralPath $candidate.executable -Algorithm SHA256).Hash
        }
        $results.Add($row)
        $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $artifactDirectory 'results.json') -Encoding UTF8
        Write-Output "$($candidate.name): exit=$resultCode failures=$($failedChecks.Count) matched=$expectationMatched"
    }
} finally {
    [AblationDisplay]::Restore()
    $brightnessControl | Invoke-CimMethod -MethodName WmiSetBrightness -Arguments @{Timeout=[uint32]0;Brightness=$savedBrightness} | Out-Null
    [void][AblationDisplay]::MagUninitialize()
}
Write-Output "Completed $($results.Count) cases; original hardware brightness=$savedBrightness."
