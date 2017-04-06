#!/usr/bin/env bash
set -e

usage() {
    echo "Usage: $0 [debug|stripped]"
    exit 1
}

check_args() {
    if [ $# -lt 1 ]; then
        usage
    fi

    type=$1

    if [ "$type" != "debug" ] && [ "$type" != "stripped" ]; then
        usage
    fi
}

main() {
    check_args $*
    if [ -z "${ENVOY_TAG}" ]; then
        ENVOY_TAG="latest"
    fi

    if [ "${type}" == "debug" ]; then
        ENVOY_IMAGE=lyft/envoy-alpine-debug:${ENVOY_TAG}
    else
        ENVOY_IMAGE=lyft/envoy-alpine:${ENVOY_TAG}
    fi
    TEST_CONTAINER=alpine_envoy_test_ct

    # Spin up a test container for the smoke test.
    set +e
    docker rm -fv ${TEST_CONTAINER}
    set -e
    docker run -d --name ${TEST_CONTAINER} ${ENVOY_IMAGE} /usr/local/bin/envoy -c /usr/local/conf/envoy/google_com_proxy.json
    output=`docker exec alpine_envoy_test_ct ps aux | grep google_com_proxy`
    if [ -z "$output" ]; then
	    echo "Failed to detect envoy binary to be running!"
	    exit 1
    fi
    echo "Successfully started envoy in container ${TEST_CONTAINER}"
    #TODO - add more tests, like a health check.
}

main $*
