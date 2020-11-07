#!/bin/bash

# Configure environment for Envoy Filter Example build and test.

set -e

# This is the hash on https://github.com/envoyproxy/envoy-filter-example.git we pin to.
ENVOY_FILTER_EXAMPLE_GITSHA="bebd0b2422ea7739905f1793565681d7266491e6"
ENVOY_FILTER_EXAMPLE_SRCDIR="${BUILD_DIR}/envoy-filter-example"

# shellcheck disable=SC2034
ENVOY_FILTER_EXAMPLE_TESTS=(
    "//:echo2_integration_test"
    "//http-filter-example:http_filter_integration_test"
    "//:envoy_binary_test")

if [[ ! -d "${ENVOY_FILTER_EXAMPLE_SRCDIR}/.git" ]]; then
  rm -rf "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
  git clone https://github.com/envoyproxy/envoy-filter-example.git "${ENVOY_FILTER_EXAMPLE_SRCDIR}"
fi

(cd "${ENVOY_FILTER_EXAMPLE_SRCDIR}" && git fetch origin && git checkout -f "${ENVOY_FILTER_EXAMPLE_GITSHA}")
sed -e "s|{ENVOY_SRCDIR}|${ENVOY_SRCDIR}|" "${ENVOY_SRCDIR}"/ci/WORKSPACE.filter.example > "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/WORKSPACE

mkdir -p "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/bazel
ln -sf "${ENVOY_SRCDIR}"/bazel/get_workspace_status "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/bazel/
cp -f "${ENVOY_SRCDIR}"/.bazelrc "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/
cp -f "$(bazel info workspace)"/*.bazelrc "${ENVOY_FILTER_EXAMPLE_SRCDIR}"/

export FILTER_WORKSPACE_SET=1
