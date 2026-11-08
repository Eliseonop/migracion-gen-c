@echo off
rem Compila y pasa los tests con MSVC (el Visual Studio registrado en vswhere) y el CMake/Ninja de CLion.
rem
rem   tools\dev\verificar_msvc.bat [preset]
rem
rem Por defecto msvc-vcpkg-debug: vcpkg en E:\vcpkg (caché binaria en E:\vcpkg-cache) con la
rem feature `media` (FFmpeg), así que compila también gc_media y sus tests. Con msvc-debug,
rem sin vcpkg ni gc_media. Sale con error si falla la compilación o algún test.
setlocal
set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=msvc-vcpkg-debug"
rem La instancia que ve vswhere, la misma que usa vcpkg: con otro compilador, vcpkg lo recompilaría todo.
set "VS="
for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%I"
if "%VS%"=="" (echo No hay ningun Visual Studio con MSVC registrado en vswhere & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
rem CLION_HOME o, si no está, el último CLion de Program Files\JetBrains (por orden alfabético).
set "CLION=%CLION_HOME%"
if "%CLION%"=="" for /d %%D in ("C:\Program Files\JetBrains\CLion *") do set "CLION=%%~fD"
set "PATH=%CLION%\bin\ninja\win\x64;%CLION%\bin\cmake\win\x64\bin;%PATH%"
set "VCPKG_ROOT=E:\vcpkg"
set "VCPKG_DEFAULT_BINARY_CACHE=E:\vcpkg-cache"
if not exist E:\vcpkg-cache mkdir E:\vcpkg-cache
cd /d "%~dp0..\.." || exit /b 1
cmake --preset %PRESET% || exit /b 1
cmake --build build/%PRESET% -- -k 0 || exit /b 1
rem Con tope por test (los de render en Debug tardan minutos): uno colgado falla en vez de esperar siempre.
ctest --test-dir build/%PRESET% -j 8 --timeout 1800 --output-on-failure
