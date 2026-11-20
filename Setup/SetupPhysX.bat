@echo off
setlocal
set "Checkout=%~dp0..\Intermediate\PhysX-4.1"
set "Sdk=%Checkout%\physx"
set "Patch=%~dp0PhysX4.1-VS2026"
set "ExpectedRevision=a2c0428acab643e60618c681b501e86f7fd558cc"
set "OverlayFiles=generate_projects.bat buildtools\cmake_generate_projects.py buildtools\presets\public\vc18win64.xml"
set "SdkConfigs=debug checked profile release"
set "OutputDir=%Sdk%\bin\win.x86_64.vc142.md"
set "BuildPending=%Checkout%\.hitori-physx-build-pending"
if not exist "%Checkout%\.git" (
    git clone --depth 1 --branch 4.1 https://github.com/NVIDIAGameWorks/PhysX.git "%Checkout%"
    if errorlevel 1 exit /b 1
)
set "Revision="
for /f %%R in ('git -C "%Checkout%" rev-parse HEAD') do set "Revision=%%R"
if /i not "%Revision%"=="%ExpectedRevision%" (
    echo Expected PhysX 4.1 revision %ExpectedRevision% in "%Checkout%". 1>&2
    exit /b 1
)

rem Accept existing installs too. Compare the actual installed patch files before copying them.
if exist "%BuildPending%" goto build
for %%F in (%OverlayFiles%) do (
    fc /b "%Patch%\%%F" "%Sdk%\%%F" >nul 2>&1
    if errorlevel 1 goto build
)
git -C "%Checkout%" apply --reverse --check "%Patch%\ccd-slerp.patch" >nul 2>&1
if errorlevel 1 goto build
call :checkOutputs
if errorlevel 1 goto build
echo PhysX 4.1 SDK is up to date. Skipping SDK generation and build.
exit /b 0

:build
rem Patching can finish before compilation fails. Retry until the whole build succeeds.
type nul >"%BuildPending%" || exit /b 1
for %%F in (%OverlayFiles%) do (
    copy /y "%Patch%\%%F" "%Sdk%\%%F" >nul
    if errorlevel 1 exit /b 1
)
git -C "%Checkout%" apply --reverse --check "%Patch%\ccd-slerp.patch" >nul 2>&1
if errorlevel 1 (
    git -C "%Checkout%" apply "%Patch%\ccd-slerp.patch"
    if errorlevel 1 exit /b 1
)
pushd "%Sdk%" || exit /b 1
call generate_projects.bat vc18win64
set "GenerateExitCode=%ERRORLEVEL%"
popd
if not "%GenerateExitCode%"=="0" exit /b %GenerateExitCode%
set "BuildDir=%Sdk%\compiler\vc18win64"
if not exist "%BuildDir%\sdk_source_bin\PhysX.vcxproj" (
    echo PhysX project generation did not produce PhysX.vcxproj. 1>&2
    exit /b 1
)
set "CMakeExe="
for /f "usebackq tokens=1,* delims==" %%K in ("%BuildDir%\CMakeCache.txt") do if "%%K"=="CMAKE_COMMAND:INTERNAL" set "CMakeExe=%%L"
if not exist "%CMakeExe%" (
    echo Cannot find the CMake executable recorded in "%BuildDir%\CMakeCache.txt". 1>&2
    exit /b 1
)

rem Build all GUI-selectable variants, reusing up-to-date SDK targets.
for %%C in (%SdkConfigs%) do (
    echo Building PhysX 4.1 SDK [%%C]...
    "%CMakeExe%" --build "%BuildDir%" --config %%C --target PhysXExtensions PhysXCooking PhysXVehicle --parallel 2
    if errorlevel 1 exit /b 1
)
call :checkOutputs
if errorlevel 1 exit /b 1
del "%BuildPending%" || exit /b 1
exit /b 0

:checkOutputs
if not exist "%Sdk%\include\PxConfig.h" exit /b 1
rem Keep this list aligned with PhysXSettings in premake5.lua.
for %%C in (%SdkConfigs%) do (
    for %%F in (PhysX_64.lib PhysX_64.dll PhysXCommon_64.lib PhysXCommon_64.dll PhysXFoundation_64.lib PhysXFoundation_64.dll PhysXCooking_64.lib PhysXCooking_64.dll PhysXExtensions_static_64.lib PhysXPvdSDK_static_64.lib PhysXVehicle_static_64.lib) do (
        for %%P in ("%OutputDir%\%%C\%%F") do (
            if not exist "%%~fP" (
                echo Missing PhysX output: %%C\%%F
                exit /b 1
            )
            if "%%~zP"=="0" (
                echo Empty PhysX output: %%C\%%F
                exit /b 1
            )
        )
    )
)
exit /b 0
