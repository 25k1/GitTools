@echo off
setlocal

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

set "CONFIG=Release"
set "BUILD_DIR=%ROOT%\build-clang"
set "DO_CLEAN=0"

set "EXTRA="

:parse_args
if "%~1"=="" goto args_done
set "ARG=%~1"
if "%ARG:~0,2%"=="-D"         (set "EXTRA=%EXTRA% %ARG%" & shift & goto parse_args)
if /I "%ARG%"=="clean"          (set "DO_CLEAN=1" & shift & goto parse_args)
if /I "%ARG%"=="Debug"          (set "CONFIG=Debug" & shift & goto parse_args)
if /I "%ARG%"=="Release"        (set "CONFIG=Release" & shift & goto parse_args)
if /I "%ARG%"=="RelWithDebInfo" (set "CONFIG=RelWithDebInfo" & shift & goto parse_args)
if /I "%ARG%"=="MinSizeRel"     (set "CONFIG=MinSizeRel" & shift & goto parse_args)
echo ERROR: unknown argument "%ARG%"
echo Usage: build.bat [Debug^|Release^|RelWithDebInfo^|MinSizeRel] [clean] [-DVAR=VALUE ...]
exit /b 1
:args_done

if defined VCToolsInstallDir goto have_msvc_env

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found.
    echo Install Visual Studio Build Tools with the "Desktop development with C++" workload.
    exit /b 1
)

set "VSINSTALL="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo ERROR: no Visual Studio installation with the C++ toolset was found.
    exit /b 1
)

call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo ERROR: failed to initialise the MSVC x64 environment.
    exit /b 1
)
:have_msvc_env

if not defined VSINSTALL goto tools_checked
set "VSCMAKE=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake"
where cmake >nul 2>nul || set "PATH=%VSCMAKE%\CMake\bin;%PATH%"
where ninja >nul 2>nul || set "PATH=%VSCMAKE%\Ninja;%PATH%"
:tools_checked

where clang-cl >nul 2>nul
if errorlevel 1 (
    echo ERROR: clang-cl was not found on PATH.
    echo Install the "C++ Clang tools for Windows" component in the Visual Studio
    echo Installer, or add an LLVM installation's bin directory to PATH.
    exit /b 1
)
where ninja >nul 2>nul
if errorlevel 1 (
    echo ERROR: ninja was not found on PATH.
    echo Install the "C++ CMake tools for Windows" component, or get Ninja from
    echo https://github.com/ninja-build/ninja/releases
    exit /b 1
)
where cmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: cmake was not found on PATH.
    exit /b 1
)

if "%DO_CLEAN%"=="1" if exist "%BUILD_DIR%" (
    echo Cleaning %BUILD_DIR%
    rmdir /s /q "%BUILD_DIR%"
)

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%CONFIG% ^
    -DCMAKE_C_COMPILER=clang-cl ^
    -DCMAKE_CXX_COMPILER=clang-cl%EXTRA%
if errorlevel 1 (
    echo.
    echo ERROR: configure failed.
    exit /b 1
)

cmake --build "%BUILD_DIR%"
if errorlevel 1 (
    echo.
    echo ERROR: build failed.
    exit /b 1
)

echo.
for %%f in ("%BUILD_DIR%\gittools.exe") do echo Built %CONFIG%: %%~ff  (%%~zf bytes)
exit /b 0
