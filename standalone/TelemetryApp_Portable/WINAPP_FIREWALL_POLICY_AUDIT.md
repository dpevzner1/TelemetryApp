# TelemetryApp Windows Defender Firewall Policy Audit

Date: 2026-07-07

## Scope

This artifact records the current installer firewall policy for TelemetryApp Fleet Manager and Sensor Client deployments. It covers Windows Defender Firewall rules created by setup, known policy blockers, validation commands, and enterprise hardening requirements.

## Current Installer Policy

Setup requests administrator elevation and shows an explicit Windows Defender Firewall permissions checkbox.

When firewall rule management is enabled:

- Rule group: `TelemetryApp`
- `TelemetryApp Client Outbound`
  - Program: `telemetry_client.exe`
  - Direction: outbound
  - Action: allow
  - Profile: Domain, Private
- `TelemetryApp Service Outbound`
  - Program: `telemetry_service.exe`
  - Direction: outbound
  - Action: allow
  - Profile: Domain, Private
- `TelemetryApp Service API Inbound`
  - Program: `telemetry_service.exe`
  - Direction: inbound
  - Action: allow
  - Protocol: TCP
  - Local port: `8765`
  - Remote address: `LocalSubnet`
  - Profile: Domain, Private
  - Created only when LAN readiness/API binding is enabled.

Public profile inbound access remains closed. Repair/reinstall/modify can remove existing TelemetryApp rules when firewall management is disabled. Uninstall removes TelemetryApp rules.

## Registry Audit

Setup records firewall state under `HKLM\SOFTWARE\TelemetryApp`:

- `FirewallRulesEnabled`
- `FirewallRulesApplied`
- `FirewallInboundApiEnabled`
- `FirewallInboundRemoteAddress`
- `FirewallRuleGroup`

## Microsoft Security Behaviors To Respect

The implementation is designed around these Windows Defender Firewall behaviors:

- Inbound connections are blocked unless an allow rule matches.
- Explicit block rules can override allow rules.
- Firewall profiles matter; TelemetryApp does not open Public-profile inbound access.
- Group Policy, MDM, Intune, EDR, VPN, or third-party firewalls can override local installer-created rules.
- Some enterprise profiles disable local firewall rule merge; in that state, installer-created rules can exist but not become effective.

References:

- Microsoft Learn: Windows Firewall rules
- Microsoft Learn: Configure Windows Firewall
- Microsoft Learn: Configure Windows Firewall with command line
- Microsoft Learn: `New-NetFirewallRule`
- Microsoft Learn: Firewall CSP

## Validation Phases

### Phase 1: Install-Time Audit

Run after install/repair/modify:

```powershell
reg query HKLM\SOFTWARE\TelemetryApp /s
Get-Service TelemetryService
Get-NetFirewallRule -DisplayName "TelemetryApp*" |
  Format-Table DisplayName,Enabled,Direction,Action,Profile,Group
Get-NetFirewallRule -DisplayName "TelemetryApp Service API Inbound" |
  Get-NetFirewallAddressFilter
```

Expected:

- Fleet Manager install writes `InstallMode=FleetHost`.
- Sensor install writes `InstallMode=SensorClient`.
- Firewall rules are grouped as `TelemetryApp`.
- Inbound rule exists only when remote API/LAN readiness is enabled.
- Inbound rule remote address is `LocalSubnet`.

### Phase 2: Policy Conflict Audit

```powershell
Get-NetConnectionProfile
Get-NetFirewallProfile |
  Select Name,Enabled,DefaultInboundAction,DefaultOutboundAction,AllowLocalFirewallRules
Get-NetFirewallRule -Action Block -Enabled True |
  Where-Object DisplayName -match "Telemetry|8765|service|client"
```

Expected:

- Active trusted LAN should be Private or Domain, not Public.
- `AllowLocalFirewallRules` should allow local merge unless enterprise policy supplies equivalent rules.
- No explicit enabled block rule should match TelemetryApp service/client traffic.

### Phase 3: Connectivity Audit

On the sensor:

```powershell
Get-Service TelemetryService
netstat -ano | findstr :8765
curl http://127.0.0.1:8765/api/v1/enrollment/readiness
```

From the Fleet Manager host:

```powershell
Test-NetConnection <sensor-ip> -Port 8765
curl http://<sensor-ip>:8765/api/v1/enrollment/readiness
```

Expected:

- Local readiness returns `product=TelemetryApp`.
- Remote readiness returns `HTTP 200`.
- The sensor appears as an untrusted candidate through Fleet Manual Add.

## Enterprise Notes

`LocalSubnet` is the safest installer default for lab and same-LAN Fleet Manual Add. If host and sensor are separated by VLANs, routed subnets, VPN overlays, or enterprise segmentation, administrators should deploy a centrally managed firewall rule with an explicit approved remote address range.

Do not open Public-profile inbound access by default. Do not create unrestricted inbound rules for all remote addresses as a default installer behavior.

## DevOps Engine Grade

Current policy after LocalSubnet hardening: `A-` for lab and small-fleet security posture.

Remaining enterprise maturity work:

- In-app readiness panel for active network profile, bind mode, firewall rule state, and local rule merge status.
- Installer postflight log under `%PROGRAMDATA%\TelemetryApp\logs\installer.log`.
- GPO/Intune deployment templates for centrally managed firewall rules.
- TLS/mTLS and enrollment trust before remote telemetry polling is enabled.
