@echo off
setlocal
set "Checkout=%~dp0..\Intermediate\PhysX-4.1"
set "Sdk=%Checkout%\physx"
set "Patch=%~dp0PhysX4.1-VS2026"
set "ExpectedRevision=a2c0428acab643e60618c681b501e86f7fd558cc"
if not defined HITORI_PHYSX_DEBUG set "HITORI_PHYSX_DEBUG=debug"
if not defined HITORI_PHYSX_RELEASE set "HITORI_PHYSX_RELEASE=checked"
if not "%HITORI_PHYSX_DEBUG%"=="debug" if not "%HITORI_PHYSX_DEBUG%"=="checked" (
    echo HITORI_PHYSX_DEBUG must be debug or checked. 1>&2
    exit /b 1
)
if not "%HITORI_PHYSX_RELEASE%"=="checked" if not "%HITORI_PHYSX_RELEASE%"=="profile" if not "%HITORI_PHYSX_RELEASE%"=="release" (
    echo HITORI_PHYSX_RELEASE must be checked, profile or release. 1>&2
    exit /b 1
)

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

for %%F in (generate_projects.bat buildtools\cmake_generate_projects.py buildtools\presets\public\vc18win64.xml) do (
    copy /y "%Patch%\%%F" "%Sdk%\%%F" >nul
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

rem Each project generation prepares the SDK variants used by Debug, Release and Shipping.
for %%C in (debug checked profile release) do set "Build_%%C="
for %%C in (%HITORI_PHYSX_DEBUG% %HITORI_PHYSX_RELEASE% release) do (
    if not defined Build_%%C (
        echo Building PhysX 4.1 SDK [%%C]...
        "%CMakeExe%" --build "%BuildDir%" --config %%C --target PhysXExtensions PhysXCooking PhysXVehicle --parallel 2
        if errorlevel 1 exit /b 1
        set "Build_%%C=done"
    )
)
exit /b 0
