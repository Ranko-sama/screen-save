@echo off
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist "%~dp0..\build\memory-map" mkdir "%~dp0..\build\memory-map"
cl /nologo /std:c++20 /utf-8 /O1 /MT /EHsc /W4 /DUNICODE /D_UNICODE "%~dp0memory-map-probe.cpp" /Fo"%~dp0..\build\memory-map\memory-map-probe.obj" /Fe"%~dp0..\build\memory-map\memory-map-probe.exe" /link User32.lib
