@echo off
set "APP_DIR=%~dp0"
set "TELEMETRY_APP_DIR=%APP_DIR%"
set "TELEMETRY_API_URL=http://localhost:8765"
set "TELEMETRY_DATA_DIR=%APP_DIR%data"
set "TELEMETRY_INSTALL_MODE=FleetHost"
set "TELEMETRY_INSTALL_ROLE=FleetHost"
set "TELEMETRY_CAN_MANAGE_FLEET=1"
start "" "%APP_DIR%TelemetryApp.exe"
