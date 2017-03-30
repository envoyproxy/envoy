#!/usr/bin/env bash
set -e

if [ -z "${ENVOY_TAG}" ]; then
	ENVOY_TAG="latest"
fi

ENVOY_IMAGE=envoy-alpine:${ENVOY_TAG}
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
