$ErrorActionPreference = "Stop";
trap { $host.SetShouldExit(1) }

if ("$env:NUM_CPUS" -eq "") {
  $env:NUM_CPUS = (Get-WmiObject -class Win32_computersystem).NumberOfLogicalProcessors
}

if ("$env:ENVOY_BAZEL_ROOT" -eq "") {
  Write-Host "ENVOY_BAZEL_ROOT must be set!"
  throw
}

mkdir -force "$env:ENVOY_BAZEL_ROOT" > $nul

$env:ENVOY_SRCDIR = [System.IO.Path]::GetFullPath("$PSScriptRoot\..")

echo "ENVOY_BAZEL_ROOT: $env:ENVOY_BAZEL_ROOT"
echo "ENVOY_SRCDIR: $env:ENVOY_SRCDIR"

$env:BAZEL_BASE_OPTIONS="--nomaster_bazelrc --output_base=$env:ENVOY_BAZEL_ROOT --bazelrc=$env:ENVOY_SRCDIR\windows\tools\bazel.rc"
$env:BAZEL_BUILD_OPTIONS="--strategy=Genrule=standalone --spawn_strategy=standalone --verbose_failures --jobs=$env:NUM_CPUS --show_task_finish $env:BAZEL_BUILD_EXTRA_OPTIONS"
$env:BAZEL_TEST_OPTIONS="$env:BAZEL_BUILD_OPTIONS --cache_test_results=no --test_output=all $env:BAZEL_EXTRA_TEST_OPTIONS"
