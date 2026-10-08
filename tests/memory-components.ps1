# Diagnostic copies only: removing a component deliberately removes its function.
# Candidates never receive a dark-mode command and exit automatically after 12 s.
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$candidateDirectory = Join-Path $project 'build/memory-components'
$records = Join-Path $PSScriptRoot 'artifacts/memory-components'
[void](New-Item -ItemType Directory -Force $candidateDirectory)
[void](New-Item -ItemType Directory -Force $records)
$sourcePath = Join-Path $PSScriptRoot 'artifacts/memory-optimization/baseline.cpp'
$source = [IO.File]::ReadAllText($sourcePath).Replace("`r`n", "`n")
function Replace-One($text, $old, $replacement) {
    if ([regex]::Matches($text, [regex]::Escape($old)).Count -ne 1) { throw "Source anchor mismatch: $old" }
    $text.Replace($old, $replacement)
}
$source = $source.Replace('L"ScreenSave"', 'L"ScreenSaveMemoryProbe"').Replace('L"Screen Save"', 'L"Screen Save memory probe"')
$source = Replace-One $source "MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'B'" 'MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F24'
$source = Replace-One $source '    if (message == WM_HOTKEY) {' "    if (message == WM_TIMER) { PostQuitMessage(0); return 0; }`n    if (message == WM_HOTKEY) {"
$source = Replace-One $source '    MSG message;' "    SetWindowTextW(window, L`"Memory probe ready`");`n    SetTimer(window, 1, 12000, nullptr);`n    MSG message;"
$source = $source.Replace('Screen Save · Ctrl+Alt+B 最暗/恢复', 'Screen Save memory diagnostic')
$variants = @('full', 'no-wmi', 'no-matrix', 'neither')
$commands = [Collections.Generic.List[string]]::new()
$commands.Add('@echo off')
$commands.Add('call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul')
foreach ($variant in $variants) {
    $code = $source
    if ($variant -in @('no-wmi','neither')) {
        $begin = $code.IndexOf('IWbemClassObject* Query(')
        $end = $code.IndexOf('void ApplyOutput(')
        $code = $code.Substring(0, $begin) + "void OpenBrightness() {}`nint ReadBrightness() { return 100; }`nvoid SetBrightness(int) {}`n`n" + $code.Substring($end)
        foreach ($line in @('IWbemServices* wmi;', 'IWbemClassObject* brightnessArguments;', 'BSTR brightnessMethodPath;', '    brightnessArguments->Release();', '    wmi->Release();', '    SysFreeString(brightnessMethodPath);', '    CoUninitialize();')) {
            $code = Replace-One $code ($line + "`n") ''
        }
    }
    if ($variant -in @('no-matrix','neither')) {
        foreach ($line in @('        MagSetFullscreenColorEffect(&colors);', '        MagShowSystemCursor(!dark);', '        MagGetFullscreenColorEffect(&originalColors);', '    MagInitialize();', '    MagUninitialize();')) {
            $code = Replace-One $code ($line + "`n") ''
        }
    }
    $prefix = Join-Path $candidateDirectory ('Mem-' + $variant)
    [IO.File]::WriteAllText($prefix + '.cpp', $code, [Text.UTF8Encoding]::new($false))
    $commands.Add("cl /nologo /std:c++20 /utf-8 /O1 /GL /MT /EHsc /W4 /DUNICODE /D_UNICODE `"$prefix.cpp`" /Fo`"$prefix.obj`" /Fe`"$prefix.exe`" /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /NODEFAULTLIB:libucrt.lib ucrt.lib User32.lib Gdi32.lib Shell32.lib Magnification.lib Ole32.lib OleAut32.lib WbemUuid.lib")
    $commands.Add('if errorlevel 1 exit /b 1')
}
$build = Join-Path $candidateDirectory 'build.cmd'
[IO.File]::WriteAllLines($build, $commands, [Text.UTF8Encoding]::new($false))
& $build *> (Join-Path $records 'build.txt')
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic build failed; see build.txt.' }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class MemoryProbeWindow {
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint pid);
}
'@
$rows = @()
$orders = @(
    @('full', 'no-wmi', 'no-matrix', 'neither'),
    @('neither', 'no-matrix', 'no-wmi', 'full'),
    @('no-matrix', 'full', 'neither', 'no-wmi')
)
for ($round = 0; $round -lt 3; ++$round) {
    foreach ($variant in $orders[$round]) {
        $prefix = Join-Path $candidateDirectory ('Mem-' + $variant)
        $process = Start-Process -FilePath ($prefix + '.exe') -WindowStyle Hidden -PassThru
        Start-Sleep -Seconds 2
        $window = [MemoryProbeWindow]::FindWindow('ScreenSaveMemoryProbe', 'Memory probe ready')
        [uint32]$windowProcess = 0
        [void][MemoryProbeWindow]::GetWindowThreadProcessId($window, [ref]$windowProcess)
        if ($windowProcess -ne $process.Id) { throw "Candidate initialization did not complete: $variant" }
        $samples = Get-Counter -Counter "\Process(Mem-$variant)\Working Set - Private", "\Process(Mem-$variant)\Private Bytes", "\Process(Mem-$variant)\Working Set" -SampleInterval 1 -MaxSamples 6
        $process.Refresh()
        $row = [ordered]@{
            variant = $variant
            round = $round + 1
            processId = $process.Id
            exeBytes = (Get-Item ($prefix + '.exe')).Length
            privateResidentBytes = ($samples.CounterSamples | Where-Object { $_.Path.EndsWith('\working set - private') } | Measure-Object CookedValue -Average).Average
            privateCommitBytes = ($samples.CounterSamples | Where-Object { $_.Path.EndsWith('\private bytes') } | Measure-Object CookedValue -Average).Average
            totalWorkingBytes = ($samples.CounterSamples | Where-Object { $_.Path.EndsWith('\working set') } | Measure-Object CookedValue -Average).Average
            cpuSecondsIncludingStartup = $process.CPU
            threads = $process.Threads.Count
        }
        if (!$process.WaitForExit(10000)) { throw "Candidate failed to exit automatically: $variant" }
        if ($process.ExitCode -ne 0) { throw "Candidate failed: $variant" }
        $rows += $row
        $rows | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $records 'rounds.json')
        $row | ConvertTo-Json -Compress
    }
}
[ordered]@{
    measuredAt = (Get-Date).ToString('o')
    baselineSourceHash = (Get-FileHash $sourcePath).Hash
    scope = 'Fresh processes after initialization; six one-second samples per run; three runs per variant. Diagnostic components intentionally absent; no dark-mode operations or working-set trimming. Production process left running.'
} | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $records 'metadata.json')
