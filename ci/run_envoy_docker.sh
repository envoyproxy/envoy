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

# The IMAGE_ID can be set to an arbitrary image ID (found with 'docker images').
[[ -z "${IMAGE_ID}" ]] && IMAGE_ID="latest"
[[ -z "${ENVOY_DOCKER_BUILD_DIR}" ]] && ENVOY_DOCKER_BUILD_DIR=/tmp/envoy-docker-build

mkdir -p "${ENVOY_DOCKER_BUILD_DIR}"
# When not operating on an existing image ID, fetch latest.
if [[ "${IMAGE_ID}" == "latest" ]]
then
  docker pull lyft/envoy-build:latest
fi
docker run -t -i -u "${USER}":"${USER_GROUP}" -v "${ENVOY_DOCKER_BUILD_DIR}":/build \
  -v "$PWD":/source lyft/envoy-build:"${IMAGE_ID}" /bin/bash -c "cd source && $*"
