@echo off
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%~dp0dist" mkdir "%~dp0dist"
if not exist "%~dp0build" mkdir "%~dp0build"
rc /nologo /I "%~dp0." /fo "%~dp0build\screen-save.res" "%~dp0screen-save.rc"
if errorlevel 1 exit /b 1
rem Use the Windows 10/11 UCRT; keep compiler support and startup code static.
cl /nologo /std:c++20 /utf-8 /O1 /GL /MT /EHsc /W4 /DUNICODE /D_UNICODE "%~dp0screen-save.cpp" "%~dp0build\screen-save.res" /Fo"%~dp0build\screen-save.obj" /Fe"%~dp0dist\ScreenSave.exe" /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /NODEFAULTLIB:libucrt.lib ucrt.lib User32.lib Gdi32.lib Shell32.lib Magnification.lib Imm32.lib
