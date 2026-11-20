@echo off
setlocal
set "Checkout=%~dp0..\Intermediate\PhysX-4.1"
set "Sdk=%Checkout%\physx"
set "Patch=%~dp0PhysX4.1-VS2026"
set "ExpectedRevision=a2c0428acab643e60618c681b501e86f7fd558cc"

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
if not exist "%Sdk%\compiler\vc18win64\sdk_source_bin\PhysX.vcxproj" (
    echo PhysX project generation did not produce PhysX.vcxproj. 1>&2
    exit /b 1
)
exit /b 0
