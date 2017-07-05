#!/bin/bash

set -e

# When running docker on a Mac, root user permissions are required.
if [[ "$OSTYPE" == "darwin"* ]]; then
	USER=root
	USER_GROUP=root
else
	USER=$(id -u)
	USER_GROUP=$(id -g)
fi

# The IMAGE_ID defaults to the CI hash but can be set to an arbitrary image ID (found with 'docker
# images').
CI_ENVOY_BUILD_SHA=$(grep "^ENVOY_BUILD_SHA" ci/ci_steps.sh | cut -d\= -f 2)
[[ -z "${IMAGE_ID}" ]] && IMAGE_ID="${CI_ENVOY_BUILD_SHA}"
[[ -z "${ENVOY_DOCKER_BUILD_DIR}" ]] && ENVOY_DOCKER_BUILD_DIR=/tmp/envoy-docker-build

mkdir -p "${ENVOY_DOCKER_BUILD_DIR}"
# Since we specify an explicit hash, docker-run will pull from the remote repo if missing.
docker run -t -i -u "${USER}":"${USER_GROUP}" -v "${ENVOY_DOCKER_BUILD_DIR}":/build \
  -v "$PWD":/source lyft/envoy-build:"${IMAGE_ID}" /bin/bash -c "cd source && $*"
