#!/bin/bash -e

export NAME=vrp-local
export MANUAL=true

# This sandbox is not really a sandbox (atm) and is only here for testing purposes.
# It is also not really structured in the same way and tends to be the hardest to fix
# when things change.

# It tests 3 things
# - run with default (prebuilt) container
# - build container with custom Envoy (mocked)
# - rebuild and run container with default (mocked) binary

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


tag_default_vrp () {
    # Image should already be present in CI
    if [[ -z "$DOCKER_NO_PULL" ]]; then
        docker pull -q envoyproxy/envoy:google-vrp-dev
    fi
    docker tag envoyproxy/envoy:google-vrp-dev envoy-google-vrp:local
}

rebuild_vrp () {
    pushd "$(dirname "${BASH_SOURCE[0]}")/../.." > /dev/null
    # Follow instructions from
    # https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/security/google_vrp#rebuilding-the-docker-image,
    # but here we just use a mock Envoy and mock tools.
    mkdir -p linux/arm64 linux/amd64 mockbin/utils

    echo "echo 'VRP PROXY MOCK 2'" > mockbin/envoy
    chmod +x mockbin/envoy
    touch mockbin/utils/su-exec

    tar czf release.tar.zst -C mockbin .
    cp release.tar.zst linux/amd64
    mv release.tar.zst linux/arm64
    touch linux/amd64/schema_validator_tool
    touch linux/arm64/schema_validator_tool

    ./ci/docker_rebuild_google-vrp.sh "$@"
    popd > /dev/null
}

test_vrp () {
    wait_for 10 bash -c "responds_with 'normal' https://localhost:10000/content -k"
    run_log "Test proxy"
    responds_with \
        normal \
        https://localhost:10000/content \
        -k
}

# Running this does not currently work with buildkit (even if that is how it is built).
export DOCKER_BUILDKIT=0

# Test running the default build
tag_default_vrp
bring_up_example
test_vrp
bring_down_example

# Test a build with custom binary
echo "echo 'VRP PROXY MOCK'" > /tmp/envoy
chmod +x /tmp/envoy
rebuild_vrp /tmp/envoy
docker run --rm --entrypoint=/bin/sh envoy-google-vrp:local -c "/usr/local/bin/envoy | grep MOCK"

# Test rebuilding and running the default build (with some mock tools)
rebuild_vrp
bring_up_example
docker run --rm --entrypoint=/bin/sh envoy-google-vrp:local -c "/usr/local/bin/envoy | grep 'MOCK 2'"
