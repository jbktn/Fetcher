# Downloads a portable C++ toolchain into .toolchain\ (no admin rights needed).
# Every archive is checked against a pinned SHA-256 before it is unpacked.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$root = Split-Path -Parent $PSScriptRoot
$toolchain = Join-Path $root '.toolchain'
$downloads = Join-Path $toolchain 'downloads'
New-Item -ItemType Directory -Force -Path $downloads | Out-Null

$tools = @(
    @{
        Name   = 'llvm-mingw'
        Url    = 'https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/llvm-mingw-20261020-ucrt-x86_64.zip'
        Sha256 = 'fd5462b7f5d93b2ff981fcd3f9c7e427e47f3203c64ea91c27bcc444eebd74cd'
        Marker = 'llvm-mingw-20261020-ucrt-x86_64\bin\clang++.exe'
    },
    @{
        Name   = 'CMake'
        Url    = 'https://github.com/Kitware/CMake/releases/download/v4.4.5/cmake-4.4.5-windows-x86_64.zip'
        Sha256 = '326c826486d46a23cf9d5a1c9ee3a567d427bdd02c4a34071937a2a675cff004'
        Marker = 'cmake-4.4.5-windows-x86_64\bin\cmake.exe'
    },
    @{
        Name   = 'Ninja'
        Url    = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip'
        Sha256 = '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'
        Marker = 'ninja.exe'
    }
)

foreach ($tool in $tools) {
    if (Test-Path (Join-Path $toolchain $tool.Marker)) {
        Write-Host "$($tool.Name): already installed"
        continue
    }

    $archive = Join-Path $downloads ($tool.Url -split '/')[-1]
    if (-not (Test-Path $archive)) {
        Write-Host "$($tool.Name): downloading $($tool.Url)"
        Invoke-WebRequest -Uri $tool.Url -OutFile $archive -UseBasicParsing
    }

    $hash = (Get-FileHash -Algorithm SHA256 -Path $archive).Hash.ToLowerInvariant()
    if ($hash -ne $tool.Sha256) {
        Remove-Item -Force $archive
        throw "$($tool.Name): checksum mismatch. Expected $($tool.Sha256), got $hash."
    }

    Write-Host "$($tool.Name): unpacking"
    # tar.exe ships with Windows 10+ and is far faster than Expand-Archive.
    & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $toolchain
    if ($LASTEXITCODE -ne 0) { throw "$($tool.Name): failed to unpack $archive" }
}

Write-Host "Toolchain ready in $toolchain"
