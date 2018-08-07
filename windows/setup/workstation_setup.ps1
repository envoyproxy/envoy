$ErrorActionPreference = "Stop";
$ProgressPreference="SilentlyContinue"

trap { $host.SetShouldExit(1) }

Start-BitsTransfer "https://aka.ms/vs/15/release/vs_buildtools.exe" "$env:TEMP\vs_buildtools.exe"

# Install VS Build Tools in a directory without spaces to work around: https://github.com/bazelbuild/bazel/issues/4496
# otherwise none of the go code will build (c++ is fine)

$vsInstallDir="c:\VSBuildTools\2017"
echo "Installing VS Build Tools..."
cmd.exe /s /c "$env:TEMP\vs_buildtools.exe --installPath $vsInstallDir --passive --wait --norestart --nocache --add Microsoft.VisualStudio.Component.VC.CoreBuildTools --add Microsoft.VisualStudio.Component.VC.Redist.14.Latest --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows10SDK --add Microsoft.VisualStudio.Component.Windows10SDK.17134"

if ($LASTEXITCODE -ne 0) {
	echo "VS Build Tools install failed: $LASTEXITCODE"
	exit $LASTEXITCODE
}
Remove-Item "$env:TEMP\vs_buildtools.exe"
echo "Done"

Set-ExecutionPolicy Bypass -Scope Process -Force; iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))

choco install make bazel cmake ninja git -y
if ($LASTEXITCODE -ne 0) {
	echo "choco install failed: $LASTEXITCODE"
	exit $LASTEXITCODE
}

$envoyBazelRootDir = "c:\_eb"

$env:ENVOY_BAZEL_ROOT=$envoyBazelRootDir
setx ENVOY_BAZEL_ROOT $envoyBazelRootDir > $nul
if ($LASTEXITCODE -ne 0) {
	exit $LASTEXITCODE
}

$env:PATH ="$env:PATH;c:\tools\msys64\usr\bin;c:\make\bin;c:\Program Files\CMake\bin;C:\Python27;c:\programdata\chocolatey\bin;C:\Program Files\Git\bin"
setx PATH $env:PATH > $nul
if ($LASTEXITCODE -ne 0) {
	exit $LASTEXITCODE
}

$env:BAZEL_VC="$vsInstallDir\VC"
setx BAZEL_VC $env:BAZEL_VC > $nul
if ($LASTEXITCODE -ne 0) {
	exit $LASTEXITCODE
}

$env:BAZEL_SH="C:\tools\msys64\usr\bin\bash.exe"
setx BAZEL_SH $env:BAZEL_SH > $nul
if ($LASTEXITCODE -ne 0) {
	exit $LASTEXITCODE
}
