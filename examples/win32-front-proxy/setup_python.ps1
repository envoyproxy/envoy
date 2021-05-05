$ErrorActionPreference = "Stop";

function DownloadAndCheck
{
    param([string]$to, [string]$url, [string]$sha256)

    Write-Host "Downloading $url to $to..."
    (New-Object System.Net.WebClient).DownloadFile($url, $to)
    $actual = (Get-FileHash -Path $to -Algorithm SHA256).Hash
    if ($actual -ne $sha256) {
        Write-Host "Download of $url to $to is invalid, expected sha256: $sha256, but got: $actual";
        exit 1
    }
    Write-Host "done."
}

function AddToPath
{
    param([string] $directory)

    $oldPath = (Get-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH).Path
    $newPath = "$oldPath;$directory"
    Set-ItemProperty -Path 'Registry::HKEY_LOCAL_MACHINE\System\CurrentControlSet\Control\Session Manager\Environment' -Name PATH -Value $newPath
    # Add to local path so subsequent commands have access to the executables they need
    $env:PATH += ";$directory"
    Write-Host "Added $directory to PATH"
}

function RunAndCheckError
{
    param([string] $exe, [string[]] $argList, [Parameter(Mandatory=$false)] $isInstaller = $false)

    Write-Host "Running '$exe $argList'..."
    if ($isInstaller) {
        Write-Host "(running as Windows software installer)"
        Start-Process $exe -ArgumentList "$argList" -Wait -NoNewWindow
    } else {
        &$exe $argList
        if ($LASTEXITCODE -ne 0) {
            Write-Host "$exe $argList exited with code $LASTEXITCODE"
            exit $LASTEXITCODE
        }
    }
    Write-Host "done."
}

# Python3 (do not install via msys2 or the MS store's flavors, this version follows Win32 semantics)
DownloadAndCheck $env:TEMP\python3-installer.exe `
                 https://www.python.org/ftp/python/3.8.5/python-3.8.5-amd64.exe `
                 cd427c7b17337d7c13761ca20877d2d8be661bd30415ddc17072a31a65a91b64
# python installer needs to be run as an installer with Start-Process
RunAndCheckError "$env:TEMP\python3-installer.exe" @("/quiet", "InstallAllUsers=1", "Include_launcher=0", "InstallLauncherAllUsers=0") $true
AddToPath $env:ProgramFiles\Python38
AddToPath $env:ProgramFiles\Python38\Scripts
