@echo off
rem Compila y pasa los tests con MSVC (Visual Studio 2026 en D:\visual) y el CMake/Ninja de CLion.
rem
rem   tools\dev\verificar_msvc.bat [preset]
rem
rem Por defecto msvc-vcpkg-debug: vcpkg en E:\vcpkg (caché binaria en E:\vcpkg-cache) con la
rem feature `media` (FFmpeg), así que compila también gc_media y sus tests. Con msvc-debug,
rem sin vcpkg ni gc_media. Sale con error si falla la compilación o algún test.
setlocal
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=msvc-vcpkg-debug"
call "D:\visual\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "CLION=C:\Program Files\JetBrains\CLion 2026.2.3"
set "PATH=%CLION%\bin\ninja\win\x64;%CLION%\bin\cmake\win\x64\bin;%PATH%"
set "VCPKG_ROOT=E:\vcpkg"
set "VCPKG_DEFAULT_BINARY_CACHE=E:\vcpkg-cache"
if not exist E:\vcpkg-cache mkdir E:\vcpkg-cache
cd /d "%~dp0..\.." || exit /b 1
cmake --preset %PRESET% || exit /b 1
cmake --build build/%PRESET% -- -k 0 || exit /b 1
ctest --test-dir build/%PRESET% -j 8 --output-on-failure
