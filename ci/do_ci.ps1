$ErrorActionPreference = "Stop";
trap { $host.SetShouldExit(1) }

. "$PSScriptRoot\build_setup.ps1"
Write-Host "building using $env:NUM_CPUS CPUs"

function bazel_debug_binary_build() {
  echo "Building..."
  pushd "$env:ENVOY_SRCDIR"
    bazel  $env:BAZEL_BASE_OPTIONS.Split(" ") build $env:BAZEL_BUILD_OPTIONS.Split(" ") -c dbg "//source/exe:envoy-static"
    $exit = $LASTEXITCODE
    if ($exit -ne 0) {
      popd
      exit $exit
    }
  popd
}

echo "bazel debug build..."
bazel_debug_binary_build
