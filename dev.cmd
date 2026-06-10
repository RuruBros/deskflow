@echo off
setlocal

set "ROOT=%~dp0"
set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
set "VS_CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not exist "%VSDEVCMD%" (
  echo VsDevCmd.bat not found: %VSDEVCMD%
  exit /b 1
)

call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%

where cmake >nul 2>nul
if errorlevel 1 (
  if not exist "%VS_CMAKE%" (
    echo cmake not found in PATH or Visual Studio bundle.
    exit /b 1
  )
  set "CMAKE_EXE=%VS_CMAKE%"
) else (
  set "CMAKE_EXE=cmake"
)

pushd "%ROOT%" >nul
if errorlevel 1 exit /b %errorlevel%

if "%~1"=="" goto build_dev
if /I "%~1"=="dev" goto build_dev
if /I "%~1"=="core" goto build_core
if /I "%~1"=="gui" goto build_gui
if /I "%~1"=="daemon" goto build_daemon
if /I "%~1"=="platform" goto build_platform
if /I "%~1"=="all" goto build_all
if /I "%~1"=="configure" goto configure
if /I "%~1"=="shell" goto shell

echo Usage: dev [dev^|core^|gui^|daemon^|platform^|all^|configure^|shell]
popd >nul
exit /b 1

:build_dev
"%CMAKE_EXE%" --build --preset dev
set "STATUS=%errorlevel%"
popd >nul
exit /b %STATUS%

:build_core
"%CMAKE_EXE%" --build --preset core
set "STATUS=%errorlevel%"
popd >nul
exit /b %STATUS%

:build_gui
"%CMAKE_EXE%" --build --preset gui
set "STATUS=%errorlevel%"
popd >nul
exit /b %STATUS%

:build_daemon
"%CMAKE_EXE%" --build --preset daemon
set "STATUS=%errorlevel%"
popd >nul
exit /b %STATUS%

:build_platform
"%CMAKE_EXE%" --build --preset platform
set "STATUS=%errorlevel%"
popd >nul
exit /b %STATUS%

:build_all
"%CMAKE_EXE%" --build --preset windows-debug-vcpkg
set "STATUS=%errorlevel%"
popd >nul
exit /b %STATUS%

:configure
"%CMAKE_EXE%" --preset dev
set "STATUS=%errorlevel%"
popd >nul
exit /b %STATUS%

:shell
echo MSVC build environment is ready.
cmd.exe /k
set "STATUS=%errorlevel%"
popd >nul
exit /b %STATUS%
