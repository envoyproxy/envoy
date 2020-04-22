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
  if ($test) {
    echo "running windows tests $test"
    bazel $env:BAZEL_BASE_OPTIONS.Split(" ") test $env:BAZEL_BUILD_OPTIONS.Split(" ") -c $type --build_tests_only $test
  } else {
    echo "running all windows tests"
    bazel $env:BAZEL_BASE_OPTIONS.Split(" ") test $env:BAZEL_BUILD_OPTIONS.Split(" ") -c $type "//test/..." --test_tag_filters=-skip_on_windows --build_tests_only --test_summary=terse --test_output=errors
  }
  exit $LASTEXITCODE
}

$action, $test = $args

switch ($action) {
  "bazel.release" {
    echo "bazel release build with tests..."
    bazel_binary_build "opt"
    bazel_test "opt" $test
  }
  "bazel.release.server_only" {
    echo "bazel release build..."
    bazel_binary_build "opt"
  }
  "bazel.release.test_only" {
    echo "bazel release build with tests..."
    bazel_test "opt" $test
  }
  "bazel.debug" {
    echo "bazel debug build with tests..."
    bazel_binary_build "dbg"
    bazel_test "dbg" $test
  }
  "bazel.debug.server_only" {
    echo "bazel debug build..."
    bazel_binary_build "dbg"
  }
  "bazel.debug.test_only" {
    echo "bazel debug build with tests..."
    bazel_test "dbg" $test
  }
  "bazel.dev" {
    echo "bazel fastbuild build with tests..."
    bazel_binary_build "fastbuild"
    bazel_test "fastbuild" $test
  }
  "bazel.dev.test_only" {
    echo "bazel fastbuild build with tests..."
    bazel_test "fastbuild" $test
  }
  default {
    echo "unknown action: $action"
    exit 1
  }
}
