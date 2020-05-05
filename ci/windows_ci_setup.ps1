# This file only installs dependencies needed in additio to Azure pipelines hosted image.
# The list of installed software can be found at:
# https://github.com/actions/virtual-environments/blob/master/images/win/Windows2019-Readme.md

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
# See https://sourceforge.net/projects/msys2/files/Base/x86_64/ for msys2 download source
$wc.DownloadFile("http://repo.msys2.org/distrib/x86_64/msys2-base-x86_64-20190524.tar.xz", "$env:TEMP\msys2.tar.xz")

# Check the SHA256 file hash of each downloaded file.
Checksum $env:TOOLS_BIN_DIR\bazel.exe 96395ee9e3fb9f4499fcaffa8a94dd72b0748f495f366bc4be44dbf09d6827fc SHA256
Checksum $env:TEMP\msys2.tar.xz 168e156fa9f00d90a8445676c023c63be6e82f71487f4e2688ab5cb13b345383 SHA256

# Unpack and install msys2 and required packages
$tarpath="$env:ProgramFiles\Git\usr\bin\tar.exe"
$msys2TarPathClean = "/$env:TEMP/msys2.tar.xz".replace(':', '').replace('\', '/')
$outDirClean = "/$env:TOOLS_BIN_DIR".replace(':', '').replace('\', '/')
&"$tarpath" -Jxf $msys2TarPathClean -C $outDirClean --strip-components=1
# Add utils to the path for msys2 setup
$env:PATH = "$env:TOOLS_BIN_DIR\usr\bin;$env:TOOLS_BIN_DIR\mingw64\bin;$env:PATH"
bash.exe -c "pacman-key --init 2>&1"
bash.exe -c "pacman-key --populate msys2 2>&1"
bash.exe -c "pacman.exe -Syyuu --noconfirm 2>&1"
bash.exe -c "pacman.exe -Syuu --noconfirm 2>&1"
bash.exe -c "pacman.exe -S --noconfirm --needed compression diffutils patch 2>&1"
bash.exe -c "pacman.exe -Scc --noconfirm 2>&1"
