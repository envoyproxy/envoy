#!/bin/bash -e

export NAME=vrp-local
export DELAY=10

# Grab an Envoy binary to use from latest dev.
CONTAINER_ID=$(docker create envoyproxy/envoy-google-vrp-dev:latest)
docker cp "${CONTAINER_ID}":/usr/local/bin/envoy /tmp/envoy
docker rm "${CONTAINER_ID}"

pushd "$(dirname "${BASH_SOURCE[0]}")"/../..
# Follow instructions from
# https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/security/google_vrp#rebuilding-the-docker-image,
# but rather than do a full build (slow), use an Envoy we built earlier.
./ci/docker_rebuild_google-vrp.sh /tmp/envoy
popd

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test proxy"
responds_with \
    normal \
    https://localhost:10000/content \
    -k
