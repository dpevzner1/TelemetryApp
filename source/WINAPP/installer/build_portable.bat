@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
set "BIN_DIR=%ROOT_DIR%\bin"
set "DIST_DIR=%ROOT_DIR%\dist\TelemetryApp_Portable"

echo ============================================================
echo  TelemetryApp -- Portable Build
echo ============================================================

REM ---- Build using validated scoped SDK path -----------------------
echo [1/4] Building native binaries...
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT_DIR%\build_scoped_sdk.ps1"
if errorlevel 1 ( echo [ERROR] Scoped SDK build failed. & exit /b 1 )

REM ---- Assemble portable package ----------------------------------
echo [3/4] Assembling portable package...
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"
mkdir "%DIST_DIR%\data"
mkdir "%DIST_DIR%\logs"
mkdir "%DIST_DIR%\dashboards"

copy "%BIN_DIR%\telemetry_service.exe" "%DIST_DIR%\" >nul
if errorlevel 1 ( echo [ERROR] telemetry_service.exe not found in %BIN_DIR% & exit /b 1 )

copy "%BIN_DIR%\telemetry_client.exe"  "%DIST_DIR%\" >nul
if errorlevel 1 ( echo [ERROR] telemetry_client.exe not found in %BIN_DIR% & exit /b 1 )

copy "%BIN_DIR%\TelemetryApp.exe"  "%DIST_DIR%\" >nul
if errorlevel 1 ( echo [ERROR] TelemetryApp.exe not found in %BIN_DIR% & exit /b 1 )

for %%F in ("%BIN_DIR%\*.dll") do copy "%%F" "%DIST_DIR%\" >nul

copy "%ROOT_DIR%\README.md" "%DIST_DIR%\" >nul
if errorlevel 1 ( echo [ERROR] README.md not found in %ROOT_DIR% & exit /b 1 )
if exist "%ROOT_DIR%\LICENSE" copy "%ROOT_DIR%\LICENSE" "%DIST_DIR%\" >nul

mkdir "%DIST_DIR%\tools" >nul 2>nul
copy "%ROOT_DIR%\tools\validate_release_manifest.ps1" "%DIST_DIR%\tools\" >nul
if errorlevel 1 (
    echo [ERROR] validate_release_manifest.ps1 not found in %ROOT_DIR%\tools
    exit /b 1
)

copy "%BIN_DIR%\API.md" "%DIST_DIR%\" >nul
if errorlevel 1 (
    echo [ERROR] API.md not found in %BIN_DIR%
    echo [ERROR] Start telemetry_service.exe once or run the service API generation path before packaging.
    exit /b 1
)

copy "%ROOT_DIR%\..\docs\WINAPP_PROJECT_PROXIMITY_MATRIX.md" "%DIST_DIR%\PROJECT_PROXIMITY_MATRIX.md" >nul
if errorlevel 1 (
    echo [ERROR] PROJECT_PROXIMITY_MATRIX.md not found in project docs folder.
    exit /b 1
)

copy "%ROOT_DIR%\..\docs\WINAPP_UI_LAYOUT_HARNESS.md" "%DIST_DIR%\WINAPP_UI_LAYOUT_HARNESS.md" >nul
if errorlevel 1 (
    echo [ERROR] WINAPP_UI_LAYOUT_HARNESS.md not found in project docs folder.
    exit /b 1
)

copy "%ROOT_DIR%\..\docs\WINAPP_FIREWALL_POLICY_AUDIT.md" "%DIST_DIR%\WINAPP_FIREWALL_POLICY_AUDIT.md" >nul
if errorlevel 1 (
    echo [ERROR] WINAPP_FIREWALL_POLICY_AUDIT.md not found in project docs folder.
    exit /b 1
)

REM ---- Generate default config ------------------------------------
echo { "api_url": "http://localhost:8765", "api_port": 8765, "data_dir": ".\data", "log_dir": ".\logs", "install_mode": "FleetHost", "install_role": "FleetHost", "can_manage_fleet": 1 } > "%DIST_DIR%\data\service.json"

REM ---- Portable launchers -----------------------------------------
(
echo @echo off
echo set "APP_DIR=%%~dp0"
echo set "TELEMETRY_APP_DIR=%%APP_DIR%%"
echo set "TELEMETRY_API_URL=http://localhost:8765"
echo set "TELEMETRY_DATA_DIR=%%APP_DIR%%data"
echo set "TELEMETRY_INSTALL_MODE=FleetHost"
echo set "TELEMETRY_INSTALL_ROLE=FleetHost"
echo set "TELEMETRY_CAN_MANAGE_FLEET=1"
echo "%%APP_DIR%%telemetry_service.exe" --console
) > "%DIST_DIR%\run_service_console.bat"

(
echo @echo off
echo set "APP_DIR=%%~dp0"
echo set "TELEMETRY_APP_DIR=%%APP_DIR%%"
echo set "TELEMETRY_API_URL=http://localhost:8765"
echo set "TELEMETRY_DATA_DIR=%%APP_DIR%%data"
echo set "TELEMETRY_INSTALL_MODE=FleetHost"
echo set "TELEMETRY_INSTALL_ROLE=FleetHost"
echo set "TELEMETRY_CAN_MANAGE_FLEET=1"
echo start "" "%%APP_DIR%%TelemetryApp.exe"
) > "%DIST_DIR%\launch_client.bat"

(
echo @echo off
echo set "APP_DIR=%%~dp0"
echo set "TELEMETRY_APP_DIR=%%APP_DIR%%"
echo set "TELEMETRY_API_URL=http://localhost:8765"
echo set "TELEMETRY_DATA_DIR=%%APP_DIR%%data"
echo set "TELEMETRY_INSTALL_MODE=FleetHost"
echo set "TELEMETRY_INSTALL_ROLE=FleetHost"
echo set "TELEMETRY_CAN_MANAGE_FLEET=1"
echo start "" "%%APP_DIR%%TelemetryApp.exe"
) > "%DIST_DIR%\launch_fleet_manager.bat"

(
echo @echo off
echo set "APP_DIR=%%~dp0"
echo set "TELEMETRY_APP_DIR=%%APP_DIR%%"
echo set "TELEMETRY_API_URL=http://localhost:8765"
echo set "TELEMETRY_DATA_DIR=%%APP_DIR%%data"
echo set "TELEMETRY_INSTALL_MODE=LocalMonitor"
echo set "TELEMETRY_INSTALL_ROLE=LocalMonitor"
echo set "TELEMETRY_CAN_MANAGE_FLEET=0"
echo start "" "%%APP_DIR%%TelemetryApp.exe"
) > "%DIST_DIR%\launch_local_monitor.bat"

(
echo @echo off
echo set "APP_DIR=%%~dp0"
echo set "TELEMETRY_APP_DIR=%%APP_DIR%%"
echo set "TELEMETRY_API_URL=http://localhost:8765"
echo set "TELEMETRY_DATA_DIR=%%APP_DIR%%data"
echo set "TELEMETRY_INSTALL_MODE=SensorClient"
echo set "TELEMETRY_INSTALL_ROLE=SensorClient"
echo set "TELEMETRY_CAN_MANAGE_FLEET=0"
echo start "" "%%APP_DIR%%TelemetryApp.exe"
) > "%DIST_DIR%\launch_sensor.bat"

echo [4/4] Portable package ready:
echo       %DIST_DIR%
echo.
echo  To run: open dist\TelemetryApp_Portable\ and run launch_fleet_manager.bat
echo          (start run_service_console.bat first, or --install the service)
echo ============================================================
endlocal
