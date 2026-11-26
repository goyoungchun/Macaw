@echo off
setlocal

set "Generator=%~1"
if not defined Generator set "Generator=vs2026"
if /i not "%Generator%"=="vs2022" if /i not "%Generator%"=="vs2026" (
    echo Usage: %~nx0 [vs2022^|vs2026] >&2
    exit /b 1
)

pushd "%~dp0" || exit /b 1
"Scripts\premake5.exe" --file=premake5.lua %Generator%
set "PremakeExitCode=%ERRORLEVEL%"
popd
exit /b %PremakeExitCode%
