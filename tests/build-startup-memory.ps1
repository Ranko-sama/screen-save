# Builds two self-closing diagnostic copies. No candidate is launched here.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$directory = Join-Path $root 'build/memory-map/reproduce-startup'
New-Item -ItemType Directory -Force -Path $directory | Out-Null
# Reproduce the historical 0/1-call comparison independently of current features.
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'artifacts/memory-map/baseline.cpp')).Replace("`r`n", "`n")
$helper = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'startup-memory.inc'))
function Replace-One($text, $old, $replacement) {
    if ([regex]::Matches($text, [regex]::Escape($old)).Count -ne 1) { throw "Source anchor mismatch: $old" }
    $text.Replace($old, $replacement)
}
$source = Replace-One $source 'constexpr UINT TrayMessage' ($helper + "`nconstexpr UINT TrayMessage")
$entry = 'int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {'
$source = Replace-One $source $entry ($entry + "`n" + '    InitMark("entry");')
# Separate diagnostic window/hotkey; the running release remains untouched.
$source = $source.Replace('L"ScreenSave"', 'L"ScreenSaveInitProbe"').Replace("MOD_NOREPEAT, 'B'", 'MOD_NOREPEAT, VK_F24')
$source = Replace-One $source '    // The fixed hotkey owns' ('    InitMark("window");' + "`n" + '    // The fixed hotkey owns')
$startup = @'
    InitMark("hotkey");
    OpenBrightness();
    InitMark("brightness");
    MagInitialize();
    InitMark("magnification");
    display = GetDC(nullptr);
    InitMark("display_dc");
'@
$source = Replace-One $source "    OpenBrightness();`n    MagInitialize();`n    display = GetDC(nullptr);" $startup
$tray = @'
    InitMark("icon_and_tip");
    Shell_NotifyIconW(NIM_ADD, &tray);
    InitMark("tray");
    SetTimer(window, 71, 1500, nullptr);
'@
$source = Replace-One $source '    Shell_NotifyIconW(NIM_ADD, &tray);' $tray
$source = Replace-One $source '    if (message == WM_HOTKEY) {' ('    if (message == WM_TIMER) { KillTimer(window, 71); InitMark("message_loop"); InitSave(); PostQuitMessage(0); return 0; }' + "`n" + '    if (message == WM_HOTKEY) {')
$commands = @('@echo off', 'call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul')
foreach ($variant in @('baseline-init', 'no-ime-init')) {
    $resultPath = (Join-Path $directory ($variant + '.json')).Replace('\', '\\')
    $code = '#define INIT_OUTPUT L"' + $resultPath + '"' + "`n" + $source
    if ($variant -eq 'no-ime-init') {
        $code = Replace-One $code '    WNDCLASSW cls{};' "    ImmDisableIME(GetCurrentThreadId());`n    WNDCLASSW cls{};"
    }
    $prefix = Join-Path $directory $variant
    [IO.File]::WriteAllText($prefix + '.cpp', $code, [Text.UTF8Encoding]::new($false))
    $commands += "cl /nologo /std:c++20 /utf-8 /O1 /GL /MT /EHsc /W4 /DUNICODE /D_UNICODE `"$prefix.cpp`" /Fo`"$prefix.obj`" /Fe`"$prefix.exe`" /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /NODEFAULTLIB:libucrt.lib ucrt.lib User32.lib Gdi32.lib Shell32.lib Magnification.lib Imm32.lib"
    $commands += 'if errorlevel 1 exit /b 1'
}
$build = Join-Path $directory 'build-startup-memory.cmd'
[IO.File]::WriteAllLines($build, $commands)
& $build
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic build failed' }
