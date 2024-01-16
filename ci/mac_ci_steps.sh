#!/bin/bash

set -e

read -ra BAZEL_BUILD_EXTRA_OPTIONS <<< "${BAZEL_BUILD_EXTRA_OPTIONS:-}"
read -ra BAZEL_EXTRA_TEST_OPTIONS <<< "${BAZEL_EXTRA_TEST_OPTIONS:-}"

BUILD_CONFIG="$(dirname "$(realpath "$0")")"/osx-build-config

# TODO(zuercher): remove --flaky_test_attempts when https://github.com/envoyproxy/envoy/issues/2428
# is resolved.
BAZEL_BUILD_OPTIONS=(
    "--curses=no"
    --verbose_failures
    "--flaky_test_attempts=integration@2"
    "--override_repository=envoy_build_config=${BUILD_CONFIG}"
    "${BAZEL_BUILD_EXTRA_OPTIONS[@]}"
    "${BAZEL_EXTRA_TEST_OPTIONS[@]}")

NCPU=$(sysctl -n hw.ncpu)
if [[ $NCPU -gt 0 ]]; then
    echo "limiting build to $NCPU jobs, based on CPU count"
    BAZEL_BUILD_OPTIONS+=("--jobs=$NCPU")
fi

# Build envoy and run tests as separate steps so that failure output
# is somewhat more deterministic (rather than interleaving the build
# and test steps).

DEFAULT_TEST_TARGETS=(
  "//test/integration:integration_test"
  "//test/integration:protocol_integration_test"
  "//test/integration:tcp_proxy_integration_test"
  "//test/integration:extension_discovery_integration_test"
  "//test/integration:listener_lds_integration_test")

if [[ $# -gt 0 ]]; then
  TEST_TARGETS=("$@")
else
  TEST_TARGETS=("${DEFAULT_TEST_TARGETS[@]}")
fi

if [[ "${TEST_TARGETS[*]}" == "${DEFAULT_TEST_TARGETS[*]}" ]]; then
  bazel build "${BAZEL_BUILD_OPTIONS[@]}" //source/exe:envoy-static
fi
bazel test "${BAZEL_BUILD_OPTIONS[@]}" "${TEST_TARGETS[@]}"

# Additionally run macOS specific test suites
bazel test "${BAZEL_BUILD_OPTIONS[@]}" //test/extensions/network/dns_resolver/apple:apple_dns_impl_test
