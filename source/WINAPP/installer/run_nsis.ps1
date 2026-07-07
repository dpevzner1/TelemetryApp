param(
    [Parameter(Mandatory)][string]$DistDir,
    [Parameter(Mandatory)][string]$NsiScript
)

$nsis = "C:\Program Files (x86)\NSIS\makensis.exe"
if (-not (Test-Path $nsis)) {
    Write-Error "NSIS not found at: $nsis"
    Write-Error "Install via: winget install NSIS.NSIS"
    exit 1
}

& $nsis "/DAPP_BINDIR=$DistDir" $NsiScript
exit $LASTEXITCODE
