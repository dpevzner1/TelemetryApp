param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $Root
$Sdk = "C:\Program Files\Microsoft Visual Studio\18\Community\SDK\ScopeCppSDK\vc15"
$CMake = Join-Path $ProjectRoot ".tools\cmake-4.3.4-windows-x86_64\bin\cmake.exe"
$Deps = Join-Path $ProjectRoot ".tools\deps"
$FakeMt = Join-Path $ProjectRoot ".tools\fake-mt.bat"

if (!(Test-Path $CMake)) { throw "Missing portable CMake at $CMake" }
if (!(Test-Path (Join-Path $Sdk "VC\bin\cl.exe"))) { throw "Missing scoped SDK compiler under $Sdk" }
if (!(Test-Path $Deps)) { throw "Missing local dependency headers at $Deps" }

$env:PATH = "$Sdk\VC\bin;$Sdk\SDK\bin;$env:PATH"
$env:INCLUDE = "$Sdk\VC\include;$Sdk\SDK\include\ucrt;$Sdk\SDK\include\shared;$Sdk\SDK\include\um"
$env:LIB = "$Sdk\VC\lib;$Sdk\SDK\lib"

& $CMake -S $Root -B (Join-Path $Root "build_release") `
    -G "NMake Makefiles" `
    -DCMAKE_BUILD_TYPE=$Configuration `
    -DCMAKE_MAKE_PROGRAM="$Sdk\VC\bin\nmake.exe" `
    -DCMAKE_CXX_COMPILER="$Sdk\VC\bin\cl.exe" `
    -DCMAKE_MT="$FakeMt" `
    -DTELEMETRY_LOCAL_DEPS_DIR="$Deps"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

& $CMake --build (Join-Path $Root "build_release") --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }
