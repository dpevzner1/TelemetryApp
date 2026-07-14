@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "DIST_DIR=%ROOT_DIR%\dist\TelemetryApp_Portable"
set "NSI_SCRIPT=%SCRIPT_DIR%installer.nsi"
set "INSTALLER_EXE=%ROOT_DIR%\dist\TelemetryApp_Setup_1.0.3.exe"

echo ============================================================
echo  TelemetryApp -- Installer Build
echo ============================================================

REM ---- Build portable first ----------------------------------------
call "%SCRIPT_DIR%build_portable.bat"
if errorlevel 1 exit /b 1

REM ---- Compile installer (NSIS path handled by PowerShell) ---------
echo [+] Compiling NSIS installer...
if exist "%INSTALLER_EXE%" del /f /q "%INSTALLER_EXE%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%run_nsis.ps1" "%DIST_DIR%" "%NSI_SCRIPT%"
if errorlevel 1 ( echo [ERROR] Installer build failed. & exit /b 1 )
if not exist "%INSTALLER_EXE%" ( echo [ERROR] Installer output missing: %INSTALLER_EXE% & exit /b 1 )

echo.
echo  Installer created: %INSTALLER_EXE%
echo ============================================================
endlocal
