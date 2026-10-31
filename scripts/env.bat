@echo off
rem Build environment: MSVC (VS2022) + Qt 6.12 + CMake/Ninja.
rem Usage: scripts\env.bat && cmake --preset debug
call "E:\Visual Studio\Product\VC\Auxiliary\Build\vcvars64.bat" >nul
set "QT_DIR=E:\Qt\6.12.0\msvc2022_64"
set "PATH=E:\Qt\6.12.0\msvc2022_64\bin;%PATH%"
