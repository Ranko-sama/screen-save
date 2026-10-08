@echo off
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist "%~dp0dist" mkdir "%~dp0dist"
if not exist "%~dp0build" mkdir "%~dp0build"
rem Use the Windows 10/11 UCRT; keep compiler support and startup code static.
cl /nologo /std:c++20 /utf-8 /O1 /GL /MT /EHsc /W4 /DUNICODE /D_UNICODE "%~dp0screen-save.cpp" /Fo"%~dp0build\screen-save.obj" /Fe"%~dp0dist\ScreenSave.exe" /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS /NODEFAULTLIB:libucrt.lib ucrt.lib User32.lib Gdi32.lib Shell32.lib Magnification.lib
