@echo off
REM TelemetryApp - one-shot update script
REM Rebuilds portable package + Windows installer, then stamps README version/date
setlocal

set VERSION=1.0.0

REM Get today's date via PowerShell (wmic is deprecated on Win 11)
for /f "delims=" %%D in ('powershell -NoProfile -Command "(Get-Date).ToString(\"yyyy-MM-dd\")"') do set BUILD_DATE=%%D

echo [update] Building portable + installer...
call "%~dp0build_installer.bat"
if ERRORLEVEL 1 (
    echo [update] ERROR: build failed.
    exit /b 1
)

echo [update] Stamping README with version %VERSION% / build %BUILD_DATE%...
powershell -NoProfile -Command ^
  "(Get-Content '%~dp0..\README.md') -replace 'Version: [0-9]+\.[0-9]+\.[0-9]+', 'Version: %VERSION%' -replace 'Build: [0-9-]+', 'Build: %BUILD_DATE%' | Set-Content '%~dp0..\README.md'"
if ERRORLEVEL 1 (
    echo [update] WARNING: README stamp failed.
)

echo.
echo [update] Done.
echo   Portable : dist\TelemetryApp_Portable\
echo   Installer: dist\TelemetryApp_Setup_%VERSION%.exe
echo   README   : stamped with version %VERSION% (%BUILD_DATE%)
echo.
endlocal
