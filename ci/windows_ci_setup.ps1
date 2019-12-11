# This file only installs dependencies needed in additio to Azure pipelines hosted image.
# The list of installed software can be found at:
# https://github.com/Microsoft/azure-pipelines-image-generation/blob/master/images/win/Vs2019-Server2019-Readme.md

mkdir "$env:TOOLS_BIN_DIR"

$wc = New-Object System.Net.WebClient
$wc.DownloadFile("https://github.com/bazelbuild/bazelisk/releases/download/v1.0/bazelisk-windows-amd64.exe", "$env:TOOLS_BIN_DIR\bazel.exe")
$wc.DownloadFile("https://github.com/ninja-build/ninja/releases/download/v1.9.0/ninja-win.zip", "$env:TOOLS_BIN_DIR\ninja-win.zip")

Add-Type -AssemblyName System.IO.Compression.FileSystem
function Unzip
{
    param([string]$zipfile, [string]$outpath)

    [System.IO.Compression.ZipFile]::ExtractToDirectory($zipfile, $outpath)
}

Unzip "$env:TOOLS_BIN_DIR\ninja-win.zip" "$env:TOOLS_BIN_DIR"
