@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: DisplayXR Example Vendor Plug-in — Local Windows Build
:: ============================================================
:: Usage: scripts\build-windows.bat [generate|build|installer|all] [config]
::   generate   - CMake configure only
::   build      - Build the plug-in DLL + install (default targets)
::   installer  - Build the NSIS installer (depends on build)
::   all        - Everything (default)
::   config     - Release (default) | RelWithDebInfo | Debug
::
:: NO vendor SDK is required — this template builds against the public
:: DisplayXR runtime ABI only.
::
:: Optional environment:
::   DXR_RUNTIME_SOURCE_DIR — path to a local displayxr-runtime checkout.
::                            If unset, probes the sibling ..\displayxr-runtime.
::                            If none is found, CMake FetchContents the runtime
::                            from GitHub at the tag pinned in CMakeLists.txt
::                            (slow first build).
:: ============================================================

set SCRIPT_DIR=%~dp0
set REPO=%SCRIPT_DIR%..\
set TARGET=%~1
if "%TARGET%"=="" set TARGET=all
set CONFIG=%~2
if "%CONFIG%"=="" set CONFIG=Release

:: --- Resolve a local runtime checkout (optional) ---
if "%DXR_RUNTIME_SOURCE_DIR%"=="" (
    if exist "%REPO%..\displayxr-runtime\CMakeLists.txt" (
        set DXR_RUNTIME_SOURCE_DIR=%REPO%..\displayxr-runtime
        echo Detected local runtime at !DXR_RUNTIME_SOURCE_DIR!
    ) else (
        echo Local runtime checkout not found next to %REPO% ^(displayxr-runtime^).
        echo CMake will FetchContent the runtime from GitHub ^(slow first build^).
    )
)

:: --- MSVC env ---
echo === Setting up MSVC environment ===
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if not defined VCVARS (
    for %%E in (Community Professional Enterprise BuildTools) do (
        if not defined VCVARS if exist "C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if not defined VCVARS (
    echo ERROR: VS 2022 with the C++ workload not found.
    exit /b 1
)
call "%VCVARS%" >nul 2>&1

:: --- Skip-to targets ---
if "%TARGET%"=="build" if exist "%REPO%build\build.ninja" goto :do_build
if "%TARGET%"=="installer" if exist "%REPO%build\build.ninja" goto :do_installer

:: --- CMake Generate ---
echo === CMake Generate ===
set CMAKE_ARGS=-S "%REPO%." -B "%REPO%build" -G "Ninja Multi-Config" -DCMAKE_INSTALL_PREFIX="%REPO%_package"
if not "%DXR_RUNTIME_SOURCE_DIR%"=="" (
    set CMAKE_ARGS=!CMAKE_ARGS! -DDXR_RUNTIME_SOURCE_DIR="%DXR_RUNTIME_SOURCE_DIR%"
    if exist "%DXR_RUNTIME_SOURCE_DIR%\vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set CMAKE_ARGS=!CMAKE_ARGS! -DCMAKE_TOOLCHAIN_FILE="%DXR_RUNTIME_SOURCE_DIR%\vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_MANIFEST_DIR="%DXR_RUNTIME_SOURCE_DIR%"
        echo Using runtime sibling's vcpkg toolchain.
    )
    if exist "%DXR_RUNTIME_SOURCE_DIR%\openxr_sdk\x64\lib\openxr_loader.lib" (
        set CMAKE_ARGS=!CMAKE_ARGS! -DOpenXR_ROOT="%DXR_RUNTIME_SOURCE_DIR%\openxr_sdk"
    )
)
cmake !CMAKE_ARGS!
if %ERRORLEVEL% NEQ 0 ( echo CMake generate FAILED & exit /b 1 )
if "%TARGET%"=="generate" goto :end

:do_build
echo === Build ===
cmake --build "%REPO%build" --config !CONFIG! --target install
if %ERRORLEVEL% NEQ 0 ( echo Build FAILED & exit /b 1 )
if "%TARGET%"=="build" goto :end

:do_installer
echo === Build Installer ===
cmake --build "%REPO%build" --config !CONFIG! --target installer
if %ERRORLEVEL% NEQ 0 ( echo Installer build FAILED & exit /b 1 )

:end
echo.
echo === Done ===
echo   Plug-in DLL:  %REPO%_package\bin\plugins\DisplayXR-ExampleVendor.dll
echo   Installer:    %REPO%_package\DisplayXRExampleVendorSetup-*.exe
endlocal
