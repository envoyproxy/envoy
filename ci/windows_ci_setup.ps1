# This file only installs dependencies needed in additio to Azure pipelines hosted image.
# The list of installed software can be found at:
# https://github.com/actions/virtual-environments/blob/master/images/win/Windows2019-Readme.md

Add-Type -AssemblyName System.IO.Compression.FileSystem
function Unzip
{
    param([string]$zipfile, [string]$outpath)

    [System.IO.Compression.ZipFile]::ExtractToDirectory($zipfile, $outpath)
}

function Checksum
{
    param([string]$filepath, [string]$expected, [string]$algorithm)

    $actual = Get-FileHash -Path $filePath -Algorithm $algorithm;
    if ($actual.Hash -eq $expected) {
        Write-Host "$filepath is valid";
    } else {
        Write-Host "$filepath is invalid, expected: $expected, but got: $actual";
        exit 1
    }
}

mkdir "$env:TOOLS_BIN_DIR"
$wc = New-Object System.Net.WebClient
$wc.DownloadFile("https://github.com/bazelbuild/bazelisk/releases/download/v1.0/bazelisk-windows-amd64.exe", "$env:TOOLS_BIN_DIR\bazel.exe")
$wc.DownloadFile("https://github.com/ninja-build/ninja/releases/download/v1.9.0/ninja-win.zip", "$env:TOOLS_BIN_DIR\ninja-win.zip")

# Check the SHA256 file hash of each downloaded file.
Checksum $env:TOOLS_BIN_DIR\bazel.exe 96395ee9e3fb9f4499fcaffa8a94dd72b0748f495f366bc4be44dbf09d6827fc SHA256
Checksum $env:TOOLS_BIN_DIR\ninja-win.zip 2d70010633ddaacc3af4ffbd21e22fae90d158674a09e132e06424ba3ab036e9 SHA256

Unzip "$env:TOOLS_BIN_DIR\ninja-win.zip" "$env:TOOLS_BIN_DIR"
