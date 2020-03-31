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
$wc.DownloadFile("https://github.com/bazelbuild/bazelisk/releases/download/v1.3.0/bazelisk-windows-amd64.exe", "$env:TOOLS_BIN_DIR\bazel.exe")
$wc.DownloadFile("https://github.com/ninja-build/ninja/releases/download/v1.10.0/ninja-win.zip", "$env:TOOLS_BIN_DIR\ninja-win.zip")

# Check the SHA256 file hash of each downloaded file.
Checksum $env:TOOLS_BIN_DIR\bazel.exe 31fa9fcf250fe64aa3c5c83b69d76e1e9571b316a58bb5c714084495623e38b0 SHA256
Checksum $env:TOOLS_BIN_DIR\ninja-win.zip 919fd158c16bf135e8a850bb4046ec1ce28a7439ee08b977cd0b7f6b3463d178 SHA256

Unzip "$env:TOOLS_BIN_DIR\ninja-win.zip" "$env:TOOLS_BIN_DIR"
