@echo off
rem HALDANE - MSVC wrapper used by tools/run_all.py on Windows.
rem Usage: tools\msvc.bat <cl arguments...>
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /W4 /std:c11 %*
