# Build-only experiment. Does not launch or change the active application.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'build/memory-optimization'
$records = Join-Path $PSScriptRoot 'artifacts/memory-optimization'
$source = [IO.File]::ReadAllText((Join-Path $output 'native-wmi.cpp')).Replace("`r`n", "`n")
$old = '    wcscpy_s(tray.szTip, L"Screen Save · Ctrl+Alt+B 最暗/恢复");'
$replacement = @'
    constexpr wchar_t tip[] = L"Screen Save · Ctrl+Alt+B 最暗/恢复";
    for (unsigned i = 0; i < sizeof(tip) / sizeof(*tip); ++i) tray.szTip[i] = tip[i];
'@
if (!$source.Contains($old)) { throw 'Source anchor missing.' }
$source = $source.Replace($old, $replacement)
$source = $source.Replace('int WINAPI wWinMain(', '__declspec(noinline) int WINAPI wWinMain(')
$source += @'

// No C++ constructors, atexit handlers or CRT allocations are used by this app.
// Initialize the compiler's stack cookie before entering any protected function.
extern "C" void __cdecl __security_init_cookie();
extern "C" __declspec(safebuffers) void WINAPI Entry() {
    __security_init_cookie();
    ExitProcess(wWinMain(GetModuleHandleW(nullptr), nullptr, nullptr, SW_HIDE));
}
'@
$prefix = Join-Path $output 'native-minimal-startup'
[IO.File]::WriteAllText($prefix + '.cpp', $source, [Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath ($prefix + '.cpp') -Destination (Join-Path $records 'native-minimal-startup.cpp')
$build = @"
@echo off
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /utf-8 /O1 /GL /MT /W4 /DUNICODE /D_UNICODE "$prefix.cpp" /Fo"$prefix.obj" /Fe"$prefix.exe" /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /ENTRY:Entry /NODEFAULTLIB libcmt.lib libvcruntime.lib User32.lib Gdi32.lib Shell32.lib Magnification.lib Kernel32.lib
"@
[IO.File]::WriteAllText((Join-Path $output 'build-minimal-startup.cmd'), $build, [Text.UTF8Encoding]::new($false))
& (Join-Path $output 'build-minimal-startup.cmd') *> (Join-Path $records 'build-minimal-startup.txt')
if ($LASTEXITCODE -ne 0) { Get-Content (Join-Path $records 'build-minimal-startup.txt'); exit $LASTEXITCODE }
Get-Item ($prefix + '.exe') | Select-Object Name,Length
