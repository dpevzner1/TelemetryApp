param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$Dist = "",
    [switch]$RequireInstaller
)

$ErrorActionPreference = "Stop"

function Fail($message) {
    throw "Release manifest validation failed: $message"
}

function Read-Text($path) {
    if (!(Test-Path -LiteralPath $path)) { Fail "Missing required file: $path" }
    return [System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $path).Path)
}

$versionHeader = Join-Path $Root "shared\app_version.h"
$installerNsi = Join-Path $Root "installer\installer.nsi"
$readme = Join-Path $Root "README.md"
$api = Join-Path $Root "bin\API.md"

if ($Dist -eq "") {
    $Dist = Join-Path $Root "dist"
}

$portableCandidates = @(
    (Join-Path $Dist "TelemetryApp_Portable"),
    (Join-Path $Dist "standalone\TelemetryApp_Portable")
)
$portableRoot = $portableCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

$versionText = Read-Text $versionHeader
$installerText = Read-Text $installerNsi
$readmeText = Read-Text $readme
if (Test-Path -LiteralPath $api) {
    $apiText = Read-Text $api
} else {
    $portableApiCandidate = if ($portableRoot) { Join-Path $portableRoot "API.md" } else { Join-Path $Dist "TelemetryApp_Portable\API.md" }
    if (Test-Path -LiteralPath $portableApiCandidate) {
        $apiText = Read-Text $portableApiCandidate
    } else {
        Fail "Missing required API guide: expected $api or $portableApiCandidate"
    }
}

if ($versionText -notmatch 'APP_VERSION\s*=\s*"([^"]+)"') { Fail "APP_VERSION not found in shared/app_version.h" }
$appVersion = $Matches[1]
if ($versionText -notmatch 'CAPABILITY_REVISION\s*=\s*"([^"]+)"') { Fail "CAPABILITY_REVISION not found in shared/app_version.h" }
$capabilityRevision = $Matches[1]

if ($installerText -notmatch '!define\s+PRODUCT_VERSION\s+"([^"]+)"') { Fail "PRODUCT_VERSION not found in installer.nsi" }
$installerVersion = $Matches[1]
if ($installerVersion -ne $appVersion) { Fail "installer.nsi version $installerVersion does not match app version $appVersion" }

if ($readmeText -notmatch [regex]::Escape("Version: $appVersion")) { Fail "README.md does not advertise version $appVersion" }
if ($readmeText -notmatch [regex]::Escape($capabilityRevision)) { Fail "README.md does not advertise capability revision $capabilityRevision" }
if ($apiText -notmatch [regex]::Escape("TelemetryApp v$appVersion")) { Fail "API.md does not advertise version $appVersion" }
if ($apiText -notmatch [regex]::Escape($capabilityRevision)) { Fail "API.md does not advertise capability revision $capabilityRevision" }

$installerCandidates = @(
    (Join-Path $Dist "TelemetryApp_Setup_$appVersion.exe"),
    (Join-Path $Dist "installer\TelemetryApp_Setup_$appVersion.exe")
)
$installerPath = $installerCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($RequireInstaller -and !(Test-Path -LiteralPath $installerPath)) {
    Fail "Expected installer not found. Checked: $($installerCandidates -join ', ')"
}

if (Test-Path -LiteralPath $portableRoot) {
    $portableReadme = Read-Text (Join-Path $portableRoot "README.md")
    $portableApi = Read-Text (Join-Path $portableRoot "API.md")
    if ($portableReadme -notmatch [regex]::Escape("Version: $appVersion")) { Fail "Portable README.md version mismatch" }
    if ($portableApi -notmatch [regex]::Escape("TelemetryApp v$appVersion")) { Fail "Portable API.md version mismatch" }
}

Write-Host "Release manifest OK: version=$appVersion capability_revision=$capabilityRevision"
