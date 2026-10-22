# Downloads the official SDL3 and SDL3_mixer Visual C++ development packages into deps\.
# Visual Studio runs this automatically on the first build (see Confidential.props).
param(
    [string]$SdlVersion = "3.4.8",
    [string]$MixerVersion = "3.2.4"
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$deps = Join-Path $PSScriptRoot "..\deps"
New-Item -ItemType Directory -Force -Path $deps | Out-Null

$packages = @(
    @{ Name = "SDL3-$SdlVersion";
       Url  = "https://github.com/libsdl-org/SDL/releases/download/release-$SdlVersion/SDL3-devel-$SdlVersion-VC.zip" },
    @{ Name = "SDL3_mixer-$MixerVersion";
       Url  = "https://github.com/libsdl-org/SDL_mixer/releases/download/release-$MixerVersion/SDL3_mixer-devel-$MixerVersion-VC.zip" }
)

foreach ($p in $packages) {
    $target = Join-Path $deps $p.Name
    if (Test-Path (Join-Path $target "include")) {
        Write-Host "$($p.Name) already in deps\"
        continue
    }
    $zip = Join-Path $deps "$($p.Name).zip"
    Write-Host "Downloading $($p.Url)"
    Invoke-WebRequest -Uri $p.Url -OutFile $zip -UseBasicParsing
    Expand-Archive -Path $zip -DestinationPath $deps -Force
    Remove-Item $zip
}
