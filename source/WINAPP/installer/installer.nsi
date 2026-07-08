; ============================================================
;  TelemetryApp NSIS Installer Script
;  Requires NSIS 3.x + the EnvVarUpdate.nsh macro
;  Build via: build_installer.bat  (sets APP_BINDIR)
; ============================================================

Unicode True
SetCompressor /SOLID lzma

; ── Product metadata ──────────────────────────────────────────────────────────
!define PRODUCT_NAME       "TelemetryApp"
!define PRODUCT_VERSION    "1.0.0"
!define PRODUCT_PUBLISHER  "Demit Pevzner"
!define PRODUCT_URL        "https://github.com/dpevzner1/TelemetryApp"
!define PRODUCT_CONTACT    "demitri.pevzner@gmail.com"
!define LAUNCHER_EXE       "TelemetryApp.exe"
!define PRODUCT_EXE        "telemetry_client.exe"
!define SERVICE_EXE        "telemetry_service.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define REG_APP_KEY        "SOFTWARE\TelemetryApp"
!define REG_APP_PATHS      "Software\Microsoft\Windows\CurrentVersion\App Paths\${PRODUCT_EXE}"

; ── MUI2 (Modern UI 2) ────────────────────────────────────────────────────────
!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

; MUI Settings
!define MUI_ABORTWARNING
!define MUI_ICON    "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON  "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Wizard\win.bmp"

; Installer pages
!insertmacro MUI_PAGE_WELCOME
Page custom AboutPageCreate
Page custom MaintenancePageCreate MaintenancePageLeave
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
Page custom DeploymentRolePageCreate DeploymentRolePageLeave
Page custom StartupOptionsPageCreate StartupOptionsPageLeave
Page custom FirewallOptionsPageCreate FirewallOptionsPageLeave
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${LAUNCHER_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Launch TelemetryApp"
!insertmacro MUI_PAGE_FINISH

; Uninstaller pages
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Var INSTALL_MODE
Var HOST_URL
Var START_MODE
Var MODE_LOCAL_RADIO
Var MODE_FLEET_RADIO
Var MODE_SENSORCLIENT_RADIO
Var START_AUTO_RADIO
Var START_MANUAL_RADIO
Var EXISTING_INSTALL_DIR
Var EXISTING_UNINSTALLER
Var MAINT_ACTION
Var MAINT_REPAIR_RADIO
Var MAINT_UNINSTALL_RADIO
Var CAN_MANAGE_FLEET
Var DISCOVERY_ENABLED
Var REMOTE_COLLECTOR_ENABLED
Var REMOTE_API_ENABLED
Var REMOTE_API_CHECK
Var FIREWALL_RULES_ENABLED
Var FIREWALL_RULES_CHECK
Var FIREWALL_OPTION

; ── Installer attributes ──────────────────────────────────────────────────────
Name              "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile           "..\dist\TelemetryApp_Setup_${PRODUCT_VERSION}.exe"
InstallDir        "$PROGRAMFILES64\${PRODUCT_NAME}"
InstallDirRegKey  HKLM "${REG_APP_PATHS}" ""
RequestExecutionLevel admin
ShowInstDetails   show
ShowUnInstDetails show

; ── Macros ───────────────────────────────────────────────────────────────────
; PATH append helper (avoids duplicates)
!macro AddToPath UnInstKey Dir
    ReadRegStr $R0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
    Push $R0
    Push "${Dir}"
    Call StrContains
    Pop $R1
    StrCmp $R1 "" 0 +2
        WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$R0;${Dir}"
!macroend

!macro RemoveFromPath Dir
    ReadRegStr $R0 HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path"
    ; Simple segment remove — removes ;Dir or Dir; from PATH
    Push $R0
    Push "${Dir}"
    Call un.RemovePathEntry
    Pop $R1
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "Path" "$R1"
!macroend

; ── Functions ─────────────────────────────────────────────────────────────────
Function StrContains
    Exch $R1   ; substring to find
    Exch
    Exch $R0   ; string to search in
    Push $R2
    Push $R3
    StrLen $R2 $R1
    StrCpy $R3 0
    loop:
        StrCpy $R2 $R0 $R2 $R3
        StrCmp $R2 $R1 found
        StrCmp $R2 "" notfound
        IntOp $R3 $R3 + 1
        Goto loop
    found:
        StrCpy $R1 $R1
        Goto done
    notfound:
        StrCpy $R1 ""
    done:
    Pop $R3
    Pop $R2
    Exch $R0
    Exch
    Exch $R1
FunctionEnd

Function un.RemovePathEntry
    Exch $R1   ; segment to remove
    Exch
    Exch $R0   ; original PATH
    ; Return original for now — full implementation via RegEx or Tcl plugin
    Push $R0
FunctionEnd

Function .onInit
    SetRegView 64
    ${GetParameters} $R0
    StrCpy $INSTALL_MODE "LocalMonitor"
    StrCpy $HOST_URL ""
    StrCpy $START_MODE "Auto"
    StrCpy $MAINT_ACTION "Install"
    StrCpy $CAN_MANAGE_FLEET "0"
    StrCpy $DISCOVERY_ENABLED "0"
    StrCpy $REMOTE_COLLECTOR_ENABLED "0"
    StrCpy $REMOTE_API_ENABLED "0"
    StrCpy $FIREWALL_RULES_ENABLED "1"
    StrCpy $FIREWALL_OPTION ""
    ${GetOptions} "$R0" "/ROLE=" $INSTALL_MODE
    ${GetOptions} "$R0" "/MODE=" $INSTALL_MODE
    ${GetOptions} "$R0" "/HOST=" $HOST_URL
    ${GetOptions} "$R0" "/START=" $START_MODE
    ${GetOptions} "$R0" "/REMOTE_API=" $REMOTE_API_ENABLED
    ${GetOptions} "$R0" "/FIREWALL=" $FIREWALL_OPTION
    ${GetOptions} "$R0" "/ACTION=" $MAINT_ACTION
    ${If} $INSTALL_MODE == "FullHost"
        ; Backward compatibility: old FullHost meant local app/service/API, not fleet management.
        StrCpy $INSTALL_MODE "LocalMonitor"
    ${EndIf}
    ${If} $INSTALL_MODE != "FleetHost"
    ${AndIf} $INSTALL_MODE != "SensorClient"
        StrCpy $INSTALL_MODE "LocalMonitor"
    ${EndIf}
    ReadRegStr $EXISTING_INSTALL_DIR HKLM "${REG_APP_KEY}" "InstallDir"
    ${If} $EXISTING_INSTALL_DIR == ""
        ReadRegStr $EXISTING_INSTALL_DIR HKLM "${PRODUCT_UNINST_KEY}" "InstallLocation"
    ${EndIf}
    ReadRegStr $EXISTING_UNINSTALLER HKLM "${PRODUCT_UNINST_KEY}" "UninstallString"
    ${If} $EXISTING_INSTALL_DIR == ""
        SetRegView 32
        ReadRegStr $EXISTING_INSTALL_DIR HKLM "${REG_APP_KEY}" "InstallDir"
        ${If} $EXISTING_INSTALL_DIR == ""
            ReadRegStr $EXISTING_INSTALL_DIR HKLM "${PRODUCT_UNINST_KEY}" "InstallLocation"
        ${EndIf}
        ReadRegStr $EXISTING_UNINSTALLER HKLM "${PRODUCT_UNINST_KEY}" "UninstallString"
        SetRegView 64
    ${EndIf}
    ${If} $EXISTING_INSTALL_DIR != ""
        StrCpy $INSTDIR "$EXISTING_INSTALL_DIR"
        ReadRegDWORD $R1 HKLM "${REG_APP_KEY}" "FirewallRulesEnabled"
        ${If} $R1 == 0
            StrCpy $FIREWALL_RULES_ENABLED "0"
        ${EndIf}
    ${EndIf}
    ${If} $FIREWALL_OPTION != ""
        StrCpy $FIREWALL_RULES_ENABLED $FIREWALL_OPTION
    ${EndIf}
FunctionEnd

Function AboutPageCreate
    !insertmacro MUI_HEADER_TEXT "About TelemetryApp" "Version, purpose, license, and support."
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}

    ${NSD_CreateLabel} 0 0 100% 12u "${PRODUCT_NAME} ${PRODUCT_VERSION} by ${PRODUCT_PUBLISHER}"
    Pop $0
    ${NSD_CreateLabel} 0 20u 100% 24u "Native Windows telemetry for local dashboards, script/API process capture, logging sessions, and future enrolled fleet sensors."
    Pop $0
    ${NSD_CreateLabel} 0 52u 100% 18u "License: open source under the included terms. The full license is shown next."
    Pop $0
    ${NSD_CreateLabel} 0 80u 100% 12u "Repository: ${PRODUCT_URL}"
    Pop $0
    ${NSD_CreateLabel} 0 100u 100% 12u "Contact: ${PRODUCT_CONTACT}"
    Pop $0
    ${NSD_CreateLabel} 0 122u 100% 18u "Security: fleet discovery and remote polling remain disabled until trusted enrollment is configured."
    Pop $0

    nsDialogs::Show
FunctionEnd

Function MaintenancePageCreate
    ${If} $EXISTING_INSTALL_DIR == ""
        !insertmacro MUI_HEADER_TEXT "New Install" "TelemetryApp is not currently installed on this device."
        nsDialogs::Create 1018
        Pop $0
        ${If} $0 == error
            Abort
        ${EndIf}

        ${NSD_CreateLabel} 0 0 100% 18u "Setup will perform a new TelemetryApp ${PRODUCT_VERSION} installation."
        Pop $0
        ${NSD_CreateGroupBox} 0 26u 100% 76u "New install"
        Pop $0
        ${NSD_CreateLabel} 12u 44u 92% 14u "The next pages choose install location, deployment role, startup behavior, and firewall permissions."
        Pop $0
        ${NSD_CreateLabel} 12u 68u 92% 16u "No existing TelemetryApp service registration was detected."
        Pop $0
        StrCpy $MAINT_ACTION "Install"
        nsDialogs::Show
        Return
    ${EndIf}

    !insertmacro MUI_HEADER_TEXT "Maintenance Options" "TelemetryApp is already installed on this device."
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}

    ${NSD_CreateLabel} 0 0 100% 12u "Choose a maintenance action. Data, API keys, dashboards, and logs are preserved."
    Pop $0

    ${NSD_CreateGroupBox} 0 20u 100% 96u "Existing installation"
    Pop $0
    ${NSD_CreateLabel} 12u 36u 92% 10u "$EXISTING_INSTALL_DIR"
    Pop $0

    ${NSD_CreateRadioButton} 12u 56u 58% 10u "Update / repair / modify role"
    Pop $MAINT_REPAIR_RADIO
    ${NSD_CreateLabel} 34u 68u 88% 18u "Refresh files, registry, environment, shortcuts, service registration, deployment role, and startup behavior."
    Pop $0
    ${NSD_CreateRadioButton} 12u 92u 34% 10u "Uninstall"
    Pop $MAINT_UNINSTALL_RADIO
    ${NSD_CreateLabel} 34u 104u 88% 10u "Remove app and service; preserve local data and logs."
    Pop $0

    ${If} $MAINT_ACTION == "Uninstall"
        ${NSD_Check} $MAINT_UNINSTALL_RADIO
    ${Else}
        StrCpy $MAINT_ACTION "Repair"
        ${NSD_Check} $MAINT_REPAIR_RADIO
    ${EndIf}

    nsDialogs::Show
FunctionEnd

Function MaintenancePageLeave
    ${If} $EXISTING_INSTALL_DIR == ""
        StrCpy $MAINT_ACTION "Install"
        Return
    ${EndIf}

    ${NSD_GetState} $MAINT_UNINSTALL_RADIO $0
    ${If} $0 == ${BST_CHECKED}
        StrCpy $MAINT_ACTION "Uninstall"
        MessageBox MB_ICONQUESTION|MB_YESNO "Uninstall TelemetryApp from:$\r$\n$EXISTING_INSTALL_DIR$\r$\n$\r$\nTelemetry data, API keys, dashboards, and logs will be preserved." IDYES do_uninstall IDNO cancel_uninstall
        do_uninstall:
            ${If} $EXISTING_UNINSTALLER == ""
                StrCpy $EXISTING_UNINSTALLER "$EXISTING_INSTALL_DIR\Uninst.exe"
            ${EndIf}
            IfFileExists "$EXISTING_UNINSTALLER" +3
                MessageBox MB_ICONSTOP "The existing uninstaller was not found:$\r$\n$EXISTING_UNINSTALLER"
                Abort
            ExecWait '"$EXISTING_UNINSTALLER"' $0
            Quit
        cancel_uninstall:
            Abort
    ${Else}
        StrCpy $MAINT_ACTION "Repair"
    ${EndIf}
FunctionEnd

Function DeploymentRolePageCreate
    !insertmacro MUI_HEADER_TEXT "TelemetryApp ${PRODUCT_VERSION} Deployment Role" "Choose which TelemetryApp package identity this device will run."
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}

    ${NSD_CreateLabel} 0 0 100% 12u "Role changes are explicit. Sensor clients cannot manage other devices."
    Pop $0

    ${NSD_CreateGroupBox} 0 20u 100% 118u "Install role"
    Pop $0
    ${NSD_CreateRadioButton} 12u 38u 45% 10u "TelemetryApp Local Monitor ${PRODUCT_VERSION}"
    Pop $MODE_LOCAL_RADIO
    ${NSD_CreateLabel} 34u 50u 88% 10u "This computer only: app, service, dashboard, API."
    Pop $0
    ${NSD_CreateRadioButton} 12u 66u 45% 10u "TelemetryApp Fleet Manager ${PRODUCT_VERSION}"
    Pop $MODE_FLEET_RADIO
    ${NSD_CreateLabel} 34u 78u 88% 10u "Future fleet manager; discovery remains off by default."
    Pop $0
    ${NSD_CreateRadioButton} 12u 94u 45% 10u "TelemetryApp Sensor ${PRODUCT_VERSION}"
    Pop $MODE_SENSORCLIENT_RADIO
    ${NSD_CreateLabel} 34u 106u 88% 10u "Reports only this device; cannot manage other devices."
    Pop $0
    ${NSD_CreateLabel} 34u 124u 88% 10u "To promote later, rerun setup and choose Update / repair / modify role."
    Pop $0

    ${If} $INSTALL_MODE == "SensorClient"
        ${NSD_Check} $MODE_SENSORCLIENT_RADIO
    ${ElseIf} $INSTALL_MODE == "FleetHost"
        ${NSD_Check} $MODE_FLEET_RADIO
    ${Else}
        ${NSD_Check} $MODE_LOCAL_RADIO
    ${EndIf}

    nsDialogs::Show
FunctionEnd

Function DeploymentRolePageLeave
    ${NSD_GetState} $MODE_SENSORCLIENT_RADIO $0
    ${If} $0 == ${BST_CHECKED}
        StrCpy $INSTALL_MODE "SensorClient"
    ${Else}
        ${NSD_GetState} $MODE_FLEET_RADIO $0
        ${If} $0 == ${BST_CHECKED}
            StrCpy $INSTALL_MODE "FleetHost"
        ${Else}
            StrCpy $INSTALL_MODE "LocalMonitor"
        ${EndIf}
    ${EndIf}

    StrCpy $CAN_MANAGE_FLEET "0"
    StrCpy $DISCOVERY_ENABLED "0"
    StrCpy $REMOTE_COLLECTOR_ENABLED "0"
    ${If} $INSTALL_MODE == "FleetHost"
        StrCpy $CAN_MANAGE_FLEET "1"
        ; Discovery, remote API binding, and collector activation stay off until secure enrollment/TLS is configured.
        StrCpy $DISCOVERY_ENABLED "0"
        StrCpy $REMOTE_COLLECTOR_ENABLED "0"
    ${EndIf}
FunctionEnd

Function StartupOptionsPageCreate
    !insertmacro MUI_HEADER_TEXT "Startup Behavior" "Choose whether monitoring starts with Windows or on demand."
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}

    ${NSD_CreateLabel} 0 0 100% 12u "This controls only the Windows service startup type."
    Pop $0

    ${NSD_CreateGroupBox} 0 20u 100% 112u "Service startup and LAN reachability"
    Pop $0
    ${NSD_CreateRadioButton} 12u 38u 92% 12u "Auto-start with Windows"
    Pop $START_AUTO_RADIO
    ${NSD_CreateLabel} 34u 53u 88% 10u "Install as automatic and start telemetry during setup."
    Pop $0
    ${NSD_CreateRadioButton} 12u 70u 92% 12u "Manual / on-demand start"
    Pop $START_MANUAL_RADIO
    ${NSD_CreateLabel} 34u 85u 88% 10u "Install as demand-start; run TelemetryApp.exe to monitor."
    Pop $0
    ${NSD_CreateCheckbox} 12u 104u 92% 12u "Allow LAN readiness/API binding for lab testing"
    Pop $REMOTE_API_CHECK
    ${NSD_CreateLabel} 34u 119u 88% 12u "Remote probing also requires the firewall permission page."
    Pop $0

    ${If} $START_MODE == "Manual"
        ${NSD_Check} $START_MANUAL_RADIO
    ${Else}
        ${NSD_Check} $START_AUTO_RADIO
    ${EndIf}
    ${If} $REMOTE_API_ENABLED == "1"
        ${NSD_Check} $REMOTE_API_CHECK
    ${EndIf}

    nsDialogs::Show
FunctionEnd

Function StartupOptionsPageLeave

    ${NSD_GetState} $START_MANUAL_RADIO $0
    ${If} $0 == ${BST_CHECKED}
        StrCpy $START_MODE "Manual"
    ${Else}
        StrCpy $START_MODE "Auto"
    ${EndIf}
    ${NSD_GetState} $REMOTE_API_CHECK $0
    ${If} $0 == ${BST_CHECKED}
        StrCpy $REMOTE_API_ENABLED "1"
    ${Else}
        StrCpy $REMOTE_API_ENABLED "0"
    ${EndIf}
FunctionEnd

Function FirewallOptionsPageCreate
    !insertmacro MUI_HEADER_TEXT "Windows Defender Firewall" "Choose whether setup may manage TelemetryApp firewall permissions."
    nsDialogs::Create 1018
    Pop $0
    ${If} $0 == error
        Abort
    ${EndIf}

    ${NSD_CreateLabel} 0 0 100% 18u "Administrator elevation is required to add or remove Windows Defender Firewall rules."
    Pop $0

    ${NSD_CreateGroupBox} 0 24u 100% 116u "Firewall permissions"
    Pop $0
    ${NSD_CreateCheckbox} 12u 44u 92% 12u "Enable TelemetryApp Windows Defender Firewall rules"
    Pop $FIREWALL_RULES_CHECK
    ${NSD_CreateLabel} 34u 62u 88% 18u "Allows TelemetryApp client and service outbound traffic on Private and Domain networks."
    Pop $0
    ${NSD_CreateLabel} 34u 84u 88% 22u "If LAN readiness/API binding is enabled, also allows inbound TCP 8765 from the local subnet only on Private and Domain networks."
    Pop $0
    ${NSD_CreateLabel} 34u 112u 88% 14u "Public network inbound access remains closed. Uninstall removes these rules."
    Pop $0

    ${If} $FIREWALL_RULES_ENABLED == "0"
    ${Else}
        ${NSD_Check} $FIREWALL_RULES_CHECK
    ${EndIf}

    nsDialogs::Show
FunctionEnd

Function FirewallOptionsPageLeave
    ${NSD_GetState} $FIREWALL_RULES_CHECK $0
    ${If} $0 == ${BST_CHECKED}
        StrCpy $FIREWALL_RULES_ENABLED "1"
    ${Else}
        StrCpy $FIREWALL_RULES_ENABLED "0"
    ${EndIf}
FunctionEnd

; ── Main installation section ─────────────────────────────────────────────────
Section "TelemetryApp (required)" SEC_MAIN
    SectionIn RO
    SetShellVarContext all
    SetRegView 64

    ; Stop running components before overwriting executables.
    DetailPrint "Stopping TelemetryApp processes before installation..."
    ExecWait 'sc stop TelemetryService' $0
    Sleep 2500
    ExecWait 'taskkill /F /T /IM telemetry_service.exe' $0
    ExecWait 'taskkill /F /T /IM telemetry_client.exe' $0
    ExecWait 'taskkill /F /T /IM TelemetryApp.exe' $0

    ; ── Files ─────────────────────────────────────────────────────────────────
    SetOutPath "$INSTDIR"
    File /oname=${LAUNCHER_EXE} "${APP_BINDIR}\${LAUNCHER_EXE}"
    File /oname=${SERVICE_EXE}  "${APP_BINDIR}\${SERVICE_EXE}"
    File /oname=${PRODUCT_EXE}  "${APP_BINDIR}\${PRODUCT_EXE}"
    File "${APP_BINDIR}\README.md"
    File "${APP_BINDIR}\LICENSE"
    File "${APP_BINDIR}\API.md"
    File "${APP_BINDIR}\PROJECT_PROXIMITY_MATRIX.md"
    File "${APP_BINDIR}\WINAPP_UI_LAYOUT_HARNESS.md"
    File "${APP_BINDIR}\WINAPP_FIREWALL_POLICY_AUDIT.md"
    File "${APP_BINDIR}\launch_client.bat"
    File "${APP_BINDIR}\launch_fleet_manager.bat"
    File "${APP_BINDIR}\launch_local_monitor.bat"
    File "${APP_BINDIR}\launch_sensor.bat"
    File "${APP_BINDIR}\run_service_console.bat"

    ; Data directories
    CreateDirectory "$APPDATA\TelemetryApp"
    CreateDirectory "$APPDATA\TelemetryApp\dashboards"
    CreateDirectory "$APPDATA\TelemetryApp"
    CreateDirectory "$APPDATA\TelemetryApp\logs"
    CreateDirectory "$APPDATA\TelemetryApp\api_keys"

    ; Default service config
    FileOpen  $0 "$APPDATA\TelemetryApp\service.json" w
    FileWrite $0 '{"api_port":8765,"data_dir":"$APPDATA\\TelemetryApp","log_dir":"$APPDATA\\TelemetryApp\\logs","install_dir":"$INSTDIR","install_role":"$INSTALL_MODE","install_mode":"$INSTALL_MODE","service_start_mode":"$START_MODE","host_url":"$HOST_URL","can_manage_fleet":$CAN_MANAGE_FLEET,"discovery_enabled":$DISCOVERY_ENABLED,"remote_collector_enabled":$REMOTE_COLLECTOR_ENABLED,"remote_api_enabled":$REMOTE_API_ENABLED,"firewall_rules_enabled":$FIREWALL_RULES_ENABLED}'
    FileClose $0

    ; ── Registry: Application info ────────────────────────────────────────────
    WriteRegStr   HKLM "${REG_APP_KEY}" ""           "$INSTDIR"
    WriteRegStr   HKLM "${REG_APP_KEY}" "Version"    "${PRODUCT_VERSION}"
    WriteRegStr   HKLM "${REG_APP_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr   HKLM "${REG_APP_KEY}" "DataDir"    "$APPDATA\TelemetryApp"
    WriteRegStr   HKLM "${REG_APP_KEY}" "LogDir"     "$APPDATA\TelemetryApp\logs"
    WriteRegStr   HKLM "${REG_APP_KEY}" "Publisher"  "${PRODUCT_PUBLISHER}"
    WriteRegStr   HKLM "${REG_APP_KEY}" "Repository" "${PRODUCT_URL}"
    WriteRegStr   HKLM "${REG_APP_KEY}" "Contact"    "${PRODUCT_CONTACT}"
    WriteRegStr   HKLM "${REG_APP_KEY}" "InstallRole" "$INSTALL_MODE"
    WriteRegStr   HKLM "${REG_APP_KEY}" "InstallMode" "$INSTALL_MODE"
    ${If} $INSTALL_MODE == "FleetHost"
        WriteRegStr HKLM "${REG_APP_KEY}" "PackageName" "TelemetryApp Fleet Manager ${PRODUCT_VERSION}"
    ${ElseIf} $INSTALL_MODE == "SensorClient"
        WriteRegStr HKLM "${REG_APP_KEY}" "PackageName" "TelemetryApp Sensor ${PRODUCT_VERSION}"
    ${Else}
        WriteRegStr HKLM "${REG_APP_KEY}" "PackageName" "TelemetryApp Local Monitor ${PRODUCT_VERSION}"
    ${EndIf}
    WriteRegStr   HKLM "${REG_APP_KEY}" "ServiceStartMode" "$START_MODE"
    WriteRegStr   HKLM "${REG_APP_KEY}" "HostUrl"    "$HOST_URL"
    WriteRegStr   HKLM "${REG_APP_KEY}" "ReadmePath" "$INSTDIR\README.md"
    WriteRegStr   HKLM "${REG_APP_KEY}" "ApiGuidePath" "$INSTDIR\API.md"
    WriteRegStr   HKLM "${REG_APP_KEY}" "ProjectMatrixPath" "$INSTDIR\PROJECT_PROXIMITY_MATRIX.md"
    WriteRegStr   HKLM "${REG_APP_KEY}" "UILayoutHarnessPath" "$INSTDIR\WINAPP_UI_LAYOUT_HARNESS.md"
    WriteRegStr   HKLM "${REG_APP_KEY}" "FirewallPolicyAuditPath" "$INSTDIR\WINAPP_FIREWALL_POLICY_AUDIT.md"
    WriteRegStr   HKLM "${REG_APP_KEY}" "InstallerVersion" "${PRODUCT_VERSION}"
    WriteRegStr   HKLM "${REG_APP_KEY}\InstallAudit" "LastAction" "$MAINT_ACTION"
    WriteRegStr   HKLM "${REG_APP_KEY}\InstallAudit" "CurrentVersion" "${PRODUCT_VERSION}"
    WriteRegStr   HKLM "${REG_APP_KEY}\InstallAudit" "LastActionResult" "Success"
    WriteRegDWORD HKLM "${REG_APP_KEY}" "ApiPort"    8765
    WriteRegDWORD HKLM "${REG_APP_KEY}" "CanManageFleet" $CAN_MANAGE_FLEET
    WriteRegDWORD HKLM "${REG_APP_KEY}" "DiscoveryEnabled" $DISCOVERY_ENABLED
    WriteRegDWORD HKLM "${REG_APP_KEY}" "RemoteCollectorEnabled" $REMOTE_COLLECTOR_ENABLED
    WriteRegDWORD HKLM "${REG_APP_KEY}" "RemoteApiEnabled" $REMOTE_API_ENABLED
    WriteRegDWORD HKLM "${REG_APP_KEY}" "FirewallRulesEnabled" $FIREWALL_RULES_ENABLED

    ; ── Windows Defender Firewall rules ─────────────────────────────────────
    ; Admin elevation is required by RequestExecutionLevel admin. Outbound rules
    ; support Fleet Host discovery/API calls. Inbound TCP 8765 is allowed only
    ; when LAN readiness/API binding is explicitly enabled, and only from the
    ; local subnet on private/domain profiles. Public networks remain closed.
    DetailPrint "Configuring Windows Defender Firewall rules..."
    ExecWait 'netsh advfirewall firewall delete rule name="TelemetryApp Client Outbound" program="$INSTDIR\${PRODUCT_EXE}"' $0
    ExecWait 'netsh advfirewall firewall delete rule name="TelemetryApp Service Outbound" program="$INSTDIR\${SERVICE_EXE}"' $0
    ExecWait 'netsh advfirewall firewall delete rule name="TelemetryApp Service API Inbound" program="$INSTDIR\${SERVICE_EXE}"' $0
    ${If} $FIREWALL_RULES_ENABLED == "1"
        ExecWait `powershell -NoProfile -ExecutionPolicy Bypass -Command "New-NetFirewallRule -DisplayName 'TelemetryApp Client Outbound' -Group 'TelemetryApp' -Direction Outbound -Action Allow -Program '$INSTDIR\${PRODUCT_EXE}' -Profile Domain,Private -Enabled True"` $0
        ExecWait `powershell -NoProfile -ExecutionPolicy Bypass -Command "New-NetFirewallRule -DisplayName 'TelemetryApp Service Outbound' -Group 'TelemetryApp' -Direction Outbound -Action Allow -Program '$INSTDIR\${SERVICE_EXE}' -Profile Domain,Private -Enabled True"` $0
        ${If} $REMOTE_API_ENABLED == "1"
            ExecWait `powershell -NoProfile -ExecutionPolicy Bypass -Command "New-NetFirewallRule -DisplayName 'TelemetryApp Service API Inbound' -Group 'TelemetryApp' -Direction Inbound -Action Allow -Program '$INSTDIR\${SERVICE_EXE}' -Protocol TCP -LocalPort 8765 -RemoteAddress LocalSubnet -Profile Domain,Private -Enabled True"` $0
            WriteRegDWORD HKLM "${REG_APP_KEY}" "FirewallInboundApiEnabled" 1
            WriteRegStr HKLM "${REG_APP_KEY}" "FirewallInboundRemoteAddress" "LocalSubnet"
        ${Else}
            WriteRegDWORD HKLM "${REG_APP_KEY}" "FirewallInboundApiEnabled" 0
            WriteRegStr HKLM "${REG_APP_KEY}" "FirewallInboundRemoteAddress" ""
        ${EndIf}
        WriteRegStr HKLM "${REG_APP_KEY}" "FirewallRuleGroup" "TelemetryApp"
        WriteRegDWORD HKLM "${REG_APP_KEY}" "FirewallRulesApplied" 1
    ${Else}
        DetailPrint "TelemetryApp firewall rule management was disabled by installer option."
        WriteRegDWORD HKLM "${REG_APP_KEY}" "FirewallInboundApiEnabled" 0
        WriteRegStr HKLM "${REG_APP_KEY}" "FirewallInboundRemoteAddress" ""
        WriteRegStr HKLM "${REG_APP_KEY}" "FirewallRuleGroup" "TelemetryApp"
        WriteRegDWORD HKLM "${REG_APP_KEY}" "FirewallRulesApplied" 0
    ${EndIf}

    ; ── Registry: App Paths (allows "telemetry_client" from Run dialog) ───────
    WriteRegStr HKLM "${REG_APP_PATHS}" ""     "$INSTDIR\${PRODUCT_EXE}"
    WriteRegStr HKLM "${REG_APP_PATHS}" "Path" "$INSTDIR"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${SERVICE_EXE}" "" "$INSTDIR\${SERVICE_EXE}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${SERVICE_EXE}" "Path" "$INSTDIR"

    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${LAUNCHER_EXE}" "" "$INSTDIR\${LAUNCHER_EXE}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${LAUNCHER_EXE}" "Path" "$INSTDIR"

    ; ── Registry: System environment variables ────────────────────────────────
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
        "TELEMETRY_APP_DIR"   "$INSTDIR"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
        "TELEMETRY_API_URL"   "http://localhost:8765"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
        "TELEMETRY_DATA_DIR"  "$APPDATA\TelemetryApp"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
        "TELEMETRY_INSTALL_MODE" "$INSTALL_MODE"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
        "TELEMETRY_INSTALL_ROLE" "$INSTALL_MODE"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
        "TELEMETRY_START_MODE" "$START_MODE"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
        "TELEMETRY_HOST_URL" "$HOST_URL"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
        "TELEMETRY_CAN_MANAGE_FLEET" "$CAN_MANAGE_FLEET"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
        "TELEMETRY_DISCOVERY_ENABLED" "$DISCOVERY_ENABLED"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
        "TELEMETRY_REMOTE_COLLECTOR_ENABLED" "$REMOTE_COLLECTOR_ENABLED"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
        "TELEMETRY_REMOTE_API_ENABLED" "$REMOTE_API_ENABLED"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
        "TELEMETRY_REMOTE_API" "$REMOTE_API_ENABLED"
    WriteRegExpandStr HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" \
        "TELEMETRY_FIREWALL_RULES_ENABLED" "$FIREWALL_RULES_ENABLED"

    ; ── PATH: add install directory ───────────────────────────────────────────
    !insertmacro AddToPath "" "$INSTDIR"

    ; Broadcast environment change to all running processes
    SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000

    ; ── Windows Service: install and start ───────────────────────────────────
    ${If} $START_MODE == "Manual"
        DetailPrint "Installing TelemetryService as demand-start service..."
        ExecWait '"$INSTDIR\${SERVICE_EXE}" --install-manual' $0
    ${Else}
        DetailPrint "Installing TelemetryService as auto-start service..."
        ExecWait '"$INSTDIR\${SERVICE_EXE}" --install-auto' $0
    ${EndIf}
    IntCmp $0 0 +2
        DetailPrint "Service install returned $0 (may already be installed)"

    ${If} $START_MODE == "Manual"
        DetailPrint "TelemetryService installed for manual startup. Run TelemetryApp.exe to start monitoring on demand."
    ${Else}
        DetailPrint "Starting TelemetryService..."
        ExecWait 'sc start TelemetryService' $0
    ${EndIf}

    ; ── Add/Remove Programs entry ─────────────────────────────────────────────
    WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "DisplayName"     "${PRODUCT_NAME}"
    WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "DisplayVersion"  "${PRODUCT_VERSION}"
    WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "Publisher"       "${PRODUCT_PUBLISHER}"
    WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "URLInfoAbout"    "${PRODUCT_URL}"
    WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "URLUpdateInfo"   "${PRODUCT_URL}"
    WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "Contact"         "${PRODUCT_CONTACT}"
    WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\Uninst.exe"
    WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "ModifyPath"      "$INSTDIR\Uninst.exe /modify"
    WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "RepairPath"      "$INSTDIR\Uninst.exe /repair"
    WriteRegStr   HKLM "${PRODUCT_UNINST_KEY}" "HelpLink"        "${PRODUCT_URL}"
    WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoModify"        0
    WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "NoRepair"        0
    ; Estimated size in KB
    WriteRegDWORD HKLM "${PRODUCT_UNINST_KEY}" "EstimatedSize"   4096

    ; ── Shortcuts ─────────────────────────────────────────────────────────────
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortcut  "$SMPROGRAMS\${PRODUCT_NAME}\Telemetry Monitor.lnk" \
                    "$INSTDIR\${LAUNCHER_EXE}" "" "$INSTDIR\${LAUNCHER_EXE}" 0
    CreateShortcut  "$SMPROGRAMS\${PRODUCT_NAME}\API Guide.lnk" \
                    "$INSTDIR\API.md"
    CreateShortcut  "$SMPROGRAMS\${PRODUCT_NAME}\README.lnk" \
                    "$INSTDIR\README.md"
    CreateShortcut  "$SMPROGRAMS\${PRODUCT_NAME}\UI Layout Harness.lnk" \
                    "$INSTDIR\WINAPP_UI_LAYOUT_HARNESS.md"
    CreateShortcut  "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk" \
                    "$INSTDIR\Uninst.exe"
    CreateShortcut  "$DESKTOP\Telemetry Monitor.lnk" \
                    "$INSTDIR\${LAUNCHER_EXE}" "" "$INSTDIR\${LAUNCHER_EXE}" 0

    ; ── Uninstaller ───────────────────────────────────────────────────────────
    WriteUninstaller "$INSTDIR\Uninst.exe"

SectionEnd

; ── Uninstaller ───────────────────────────────────────────────────────────────
Section "Uninstall"
    SetShellVarContext all
    SetRegView 64

    ; Stop and remove service
    DetailPrint "Stopping TelemetryApp processes..."
    ExecWait 'sc stop TelemetryService'
    Sleep 2000
    ExecWait 'taskkill /F /T /IM telemetry_client.exe'
    ExecWait 'taskkill /F /T /IM TelemetryApp.exe'
    ExecWait 'taskkill /F /T /IM telemetry_service.exe'
    ExecWait '"$INSTDIR\${SERVICE_EXE}" --uninstall'

    ; Remove Windows Defender Firewall rules installed by setup.
    DetailPrint "Removing TelemetryApp firewall rules..."
    ExecWait 'netsh advfirewall firewall delete rule name="TelemetryApp Client Outbound"'
    ExecWait 'netsh advfirewall firewall delete rule name="TelemetryApp Service Outbound"'
    ExecWait 'netsh advfirewall firewall delete rule name="TelemetryApp Service API Inbound"'

    ; Remove environment variables
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "TELEMETRY_APP_DIR"
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "TELEMETRY_API_URL"
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "TELEMETRY_DATA_DIR"
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "TELEMETRY_INSTALL_MODE"
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "TELEMETRY_INSTALL_ROLE"
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "TELEMETRY_START_MODE"
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "TELEMETRY_HOST_URL"
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "TELEMETRY_CAN_MANAGE_FLEET"
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "TELEMETRY_DISCOVERY_ENABLED"
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "TELEMETRY_REMOTE_COLLECTOR_ENABLED"
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "TELEMETRY_REMOTE_API_ENABLED"
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "TELEMETRY_REMOTE_API"
    DeleteRegValue HKLM "SYSTEM\CurrentControlSet\Control\Session Manager\Environment" "TELEMETRY_FIREWALL_RULES_ENABLED"

    ; Remove from PATH
    !insertmacro RemoveFromPath "$INSTDIR"
    SendMessage ${HWND_BROADCAST} ${WM_SETTINGCHANGE} 0 "STR:Environment" /TIMEOUT=5000

    ; Remove registry keys
    DeleteRegKey HKLM "${REG_APP_KEY}"
    DeleteRegKey HKLM "${REG_APP_PATHS}"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${SERVICE_EXE}"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${LAUNCHER_EXE}"
    DeleteRegKey HKLM "${PRODUCT_UNINST_KEY}"

    ; Remove files
    Delete "$INSTDIR\${LAUNCHER_EXE}"
    Delete "$INSTDIR\${SERVICE_EXE}"
    Delete "$INSTDIR\${PRODUCT_EXE}"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\API.md"
    Delete "$INSTDIR\PROJECT_PROXIMITY_MATRIX.md"
    Delete "$INSTDIR\WINAPP_UI_LAYOUT_HARNESS.md"
    Delete "$INSTDIR\WINAPP_FIREWALL_POLICY_AUDIT.md"
    Delete "$INSTDIR\launch_client.bat"
    Delete "$INSTDIR\launch_fleet_manager.bat"
    Delete "$INSTDIR\launch_local_monitor.bat"
    Delete "$INSTDIR\launch_sensor.bat"
    Delete "$INSTDIR\run_service_console.bat"
    Delete "$INSTDIR\Uninst.exe"
    RMDir  "$INSTDIR"

    ; Remove shortcuts
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\Telemetry Monitor.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\API Guide.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\README.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\UI Layout Harness.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk"
    RMDir  "$SMPROGRAMS\${PRODUCT_NAME}"
    Delete "$DESKTOP\Telemetry Monitor.lnk"

    ; NOTE: $APPDATA\TelemetryApp\  (logs, api_keys, config) is deliberately
    ; preserved so user data survives uninstall/reinstall. Manual cleanup only.

SectionEnd

