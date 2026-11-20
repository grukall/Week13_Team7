@echo off
setlocal
call "%~dp0Setup\SetupPhysX.bat"
if errorlevel 1 goto done
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup\SetupFbxSdk.ps1" -GenerateProjects
:done
set "GenerateExitCode=%ERRORLEVEL%"
echo.
if /I not "%~1"=="--no-pause" pause
exit /b %GenerateExitCode%
