#!/bin/bash

set -e

. ci/envoy_build_sha.sh

# When running docker on a Mac, root user permissions are required.
if [[ "$OSTYPE" == "darwin"* ]]; then
	USER=root
	USER_GROUP=root
else
	USER=$(id -u)
	USER_GROUP=$(id -g)
fi

[[ -z "${IMAGE_NAME}" ]] && IMAGE_NAME="envoyproxy/envoy-build-ubuntu"
# The IMAGE_ID defaults to the CI hash but can be set to an arbitrary image ID (found with 'docker
# images').
[[ -z "${IMAGE_ID}" ]] && IMAGE_ID="${ENVOY_BUILD_SHA}"
[[ -z "${ENVOY_DOCKER_BUILD_DIR}" ]] && ENVOY_DOCKER_BUILD_DIR=/tmp/envoy-docker-build

mkdir -p "${ENVOY_DOCKER_BUILD_DIR}"
# Since we specify an explicit hash, docker-run will pull from the remote repo if missing.
docker run --rm -t -i -u "${USER}":"${USER_GROUP}" -v "${ENVOY_DOCKER_BUILD_DIR}":/build \
  -v "$PWD":/source -e NUM_CPUS --cap-add SYS_PTRACE "${IMAGE_NAME}":"${IMAGE_ID}" \
  /bin/bash -lc "cd source && $*"
