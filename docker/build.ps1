# docker/build.ps1 — PowerShell equivalent of build.sh for Windows hosts.
# Usage:
#   .\build.ps1 -Arch aarch64 -SdkTarball C:\path\to\sdk.tar.gz
#   .\build.ps1 -Arch armhf -SdkTarball C:\path\to\rv1106-sdk.tar.gz -AppSrc templates\cpp-mpp
param(
    [Parameter(Mandatory=$true)][string]$Arch = "aarch64",
    [Parameter(Mandatory=$true)][string]$SdkTarball,
    [string]$AppSrc = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not (Test-Path $SdkTarball)) { throw "SDK tarball not found: $SdkTarball" }

switch ($Arch) {
    "aarch64" { $Prefix = "aarch64-linux-gnu" }
    "armhf"   { $Prefix = "arm-linux-gnueabihf" }
    default   { throw "unsupported arch: $Arch (use aarch64|armhf)" }
}

$SdkName = Split-Path -Leaf $SdkTarball
$Ctx = Split-Path -Parent $SdkTarball

Write-Host "=== 1/3 base ==="
docker build -f "$ScriptDir\Dockerfile.base" -t "rk-base:$Arch" `
    --build-arg ARCH=$Arch --build-arg GCC_PREFIX=$Prefix $ScriptDir

Write-Host "=== 2/3 sdk ==="
docker build -f "$ScriptDir\Dockerfile.sdk" -t "rk-sdk:$Arch" `
    --build-arg ARCH=$Arch --build-arg GCC_PREFIX=$Prefix `
    --build-arg SDK_TARBALL=$SdkName $Ctx

if ($AppSrc) {
    Write-Host "=== 3/3 final (app=$AppSrc) ==="
    docker build -f "$ScriptDir\Dockerfile.final" -t "rk-app:$Arch" `
        --build-arg ARCH=$Arch --build-arg GCC_PREFIX=$Prefix `
        --build-arg SDK_TARBALL=$SdkName `
        --build-arg APP_SRC=$AppSrc $Ctx
}

Write-Host "done. images: rk-base:$Arch rk-sdk:$Arch$(if ($AppSrc) { " rk-app:$Arch" })"