param([string[]]$Names = @('baseline','wmi-scoped','mag-scoped','both-scoped'))
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'build/memory-optimization'
$records = Join-Path $PSScriptRoot 'artifacts/memory-optimization'
$baseline = [IO.File]::ReadAllText((Join-Path $records 'baseline.cpp')).Replace("`r`n", "`n")
function Replace-One($text, $old, $replacement) {
    if ([regex]::Matches($text, [regex]::Escape($old)).Count -ne 1) { throw "Source anchor mismatch: $old" }
    $text.Replace($old, $replacement)
}
$close = @'
void CloseBrightness() {
    brightnessArguments->Release();
    wmi->Release();
    SysFreeString(brightnessMethodPath);
    CoUninitialize();
}

'@
$commands = [Collections.Generic.List[string]]::new()
$commands.Add('@echo off')
$commands.Add('call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul')
foreach ($name in $Names) {
    $code = $baseline
    if ($name -eq 'native-wmi') {
        $code = Replace-One $code '#include <wbemidl.h>' "#include <wmistr.h>`n#include <cstring>"
        foreach ($line in @('IWbemServices* wmi;', 'IWbemClassObject* brightnessArguments;', 'BSTR brightnessMethodPath;')) {
            $code = Replace-One $code ($line + "`n") ''
        }
        $begin = $code.IndexOf('IWbemClassObject* Query(')
        $end = $code.IndexOf('void ApplyOutput(')
        $native = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'native-wmi-brightness.inc')).Replace("`r`n", "`n")
        $code = $code.Substring(0, $begin) + $native + "`n" + $code.Substring($end)
        $begin = $code.LastIndexOf('    brightnessArguments->Release();')
        $end = $code.IndexOf('    UnregisterHotKey(', $begin)
        $code = $code.Remove($begin, $end - $begin).Insert($begin, "    CloseBrightness();`n")
    }
    if ($name -in @('wmi-scoped','both-scoped')) {
        $code = Replace-One $code 'void ApplyOutput(' ($close + "`nvoid ApplyOutput(")
        $code = Replace-One $code 'void ToggleBlack() {' "void ToggleBlack() {`n    const bool needsBrightness = !isDark || dimmingMode == DimmingMode::Gamma;`n    if (needsBrightness) OpenBrightness();"
        $code = Replace-One $code '    ApplyOutput(dimmingMode, isDark);' "    ApplyOutput(dimmingMode, isDark);`n    if (needsBrightness) CloseBrightness();"
        $code = Replace-One $code "    if (isDark) {`n        ApplyOutput(mode, true);" "    if (isDark) {`n        OpenBrightness();`n        ApplyOutput(mode, true);"
        $code = Replace-One $code '        ApplyOutput(dimmingMode, false);' "        ApplyOutput(dimmingMode, false);`n        CloseBrightness();"
        $code = Replace-One $code "    OpenBrightness();`n    MagInitialize();" '    MagInitialize();'
        $begin = $code.LastIndexOf('    brightnessArguments->Release();')
        $end = $code.IndexOf('    UnregisterHotKey(', $begin)
        $code = $code.Remove($begin, $end - $begin)
    }
    if ($name -in @('mag-scoped','both-scoped')) {
        $code = Replace-One $code "    if (!isDark) {`n        brightnessBeforeDark" "    if (!isDark) {`n        MagInitialize();`n        brightnessBeforeDark"
        # A Gamma session still captures the original matrix at the same time.
        $code = Replace-One $code '    isDark = !isDark;' "    if (isDark && dimmingMode == DimmingMode::Gamma) MagInitialize();`n    isDark = !isDark;"
        $code = Replace-One $code '    ApplyOutput(dimmingMode, isDark);' "    ApplyOutput(dimmingMode, isDark);`n    if (!isDark || dimmingMode == DimmingMode::Gamma) MagUninitialize();"
        $code = Replace-One $code '        ApplyOutput(mode, true);' "        if (dimmingMode == DimmingMode::Gamma) MagInitialize();`n        ApplyOutput(mode, true);"
        $code = Replace-One $code '        ApplyOutput(dimmingMode, false);' "        ApplyOutput(dimmingMode, false);`n        if (mode == DimmingMode::Gamma) MagUninitialize();"
        $code = Replace-One $code "    MagInitialize();`n    display = GetDC(nullptr);" '    display = GetDC(nullptr);'
        $code = Replace-One $code "    MagUninitialize();`n    ReleaseDC(nullptr, display);" '    ReleaseDC(nullptr, display);'
    }
    $prefix = Join-Path $output $name
    [IO.File]::WriteAllText($prefix + '.cpp', $code, [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath ($prefix + '.cpp') -Destination (Join-Path $records ($name + '.cpp'))
    $commands.Add("cl /nologo /std:c++20 /utf-8 /O1 /GL /MT /EHsc /W4 /DUNICODE /D_UNICODE `"$prefix.cpp`" /Fo`"$prefix.obj`" /Fe`"$prefix.exe`" /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /NODEFAULTLIB:libucrt.lib ucrt.lib User32.lib Gdi32.lib Shell32.lib Magnification.lib Ole32.lib OleAut32.lib WbemUuid.lib")
    $commands.Add('if errorlevel 1 exit /b 1')
}
$build = Join-Path $output 'build-candidates.cmd'
[IO.File]::WriteAllLines($build, $commands, [Text.UTF8Encoding]::new($false))
& $build *> (Join-Path $records 'build-scoped.txt')
if ($LASTEXITCODE -ne 0) { throw 'Candidate build failed.' }
foreach ($name in $Names) { Get-Item (Join-Path $output ($name + '.exe')) | Select-Object Name,Length }
