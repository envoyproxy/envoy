#!/bin/bash -e

export NAME=vrp-local
export DELAY=10

pushd "$(dirname "${BASH_SOURCE[0]}")"/../..
# Follow instructions from
# https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/security/google_vrp#rebuilding-the-docker-image
bazel build //source/exe:envoy-static
./ci/docker_rebuild_google-vrp.sh bazel-bin/source/exe/envoy-static
popd

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test proxy"
responds_with \
    normal \
    https://localhost:10000/content \
    -k
