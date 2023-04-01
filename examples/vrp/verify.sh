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
    mkdir -p linux/amd64/build_envoy_release/
    mkdir -p linux/amd64/build_envoy_release_stripped/
    touch linux/amd64/build_envoy_release/envoy
    echo "echo 'VRP PROXY MOCK 2'" > linux/amd64/build_envoy_release_stripped/envoy
    chmod +x linux/amd64/build_envoy_release_stripped/envoy
    touch linux/amd64/build_envoy_release_stripped/envoy
    touch linux/amd64/build_envoy_release/su-exec
    touch linux/amd64/build_envoy_release/schema_validator_tool
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
