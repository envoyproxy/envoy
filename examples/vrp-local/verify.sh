#!/bin/bash -e

export NAME=vrp-local
export MANUAL=true

# This sandbox is not really a sandbox and is only here for testing purposes.
# It is also not really structured in the same way and tends to be the hardest to fix
# when things change.
#
# It does not currently working with buildkit.
export DOCKER_BUILDKIT=0

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

bring_up_example

wait_for 10 bash -c "responds_with 'normal' https://localhost:10000/content -k"

run_log "Test proxy"
responds_with \
    normal \
    https://localhost:10000/content \
    -k
