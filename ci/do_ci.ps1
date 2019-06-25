$ErrorActionPreference = "Stop";
trap { $host.SetShouldExit(1) }

. "$PSScriptRoot\build_setup.ps1"
Write-Host "building using $env:NUM_CPUS CPUs"

function bazel_binary_build($type) {
  echo "Building..."
  bazel  $env:BAZEL_BASE_OPTIONS.Split(" ") build $env:BAZEL_BUILD_OPTIONS.Split(" ") -c $type "//source/exe:envoy-static"
  $exit = $LASTEXITCODE
  if ($exit -ne 0) {
    exit $exit
  }
}

function bazel_test($type, $test) {
  if ($test -ne "") {
    bazel $env:BAZEL_BASE_OPTIONS.Split(" ") test $env:BAZEL_BUILD_OPTIONS.Split(" ") -c $type $test
  } else {
    echo "running windows tests"
    bazel $env:BAZEL_BASE_OPTIONS.Split(" ") test $env:BAZEL_BUILD_OPTIONS.Split(" ") -c $type "//test/..."
  }
  exit $LASTEXITCODE
}

$action = $args[0]
$test = $args[1]

switch ($action) {
  "bazel.release" {
    echo "bazel release build with tests..."
    bazel_binary_build "opt"
    bazel_test "opt" "$test"
  }
  "bazel.release.server_only" {
    echo "bazel release build..."
    bazel_binary_build "opt"
  }
  "bazel.debug" {
    echo "bazel debug build with tests..."
    bazel_binary_build "dbg"
    bazel_test "dbg" "$test"
  }
  "bazel.debug.server_only" {
    echo "bazel debug build..."
    bazel_binary_build "dbg"
  }
  "bazel.dev" {
    echo "bazel fastbuild build with tests..."
    bazel_binary_build "fastbuild"
    bazel_test "fastbuild" "$test"
  }
  default {
    echo "unknown action: $action"
    exit 1
  }
}
