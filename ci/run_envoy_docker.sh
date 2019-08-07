#!/bin/bash

set -e

. ci/envoy_build_sha.sh

# We run as root and later drop permissions. This is required to setup the USER
# in useradd below, which is need for correct Python execution in the Docker
# environment.
USER=root
USER_GROUP=root

[[ -z "${IMAGE_NAME}" ]] && IMAGE_NAME="envoyproxy/envoy-build-ubuntu"
# The IMAGE_ID defaults to the CI hash but can be set to an arbitrary image ID (found with 'docker
# images').
[[ -z "${IMAGE_ID}" ]] && IMAGE_ID="${ENVOY_BUILD_SHA}"
[[ -z "${ENVOY_DOCKER_BUILD_DIR}" ]] && ENVOY_DOCKER_BUILD_DIR=/tmp/envoy-docker-build

[[ -f .git ]] && [[ ! -d .git ]] && GIT_VOLUME_OPTION="-v $(git rev-parse --git-common-dir):$(git rev-parse --git-common-dir)"

[[ -t 1 ]] && DOCKER_TTY_OPTION=-it

mkdir -p "${ENVOY_DOCKER_BUILD_DIR}"
# Since we specify an explicit hash, docker-run will pull from the remote repo if missing.
docker run --rm ${DOCKER_TTY_OPTION} -e HTTP_PROXY=${http_proxy} -e HTTPS_PROXY=${https_proxy} \
  -u "${USER}":"${USER_GROUP}" -v "${ENVOY_DOCKER_BUILD_DIR}":/build ${GIT_VOLUME_OPTION} \
  -e BAZEL_BUILD_EXTRA_OPTIONS -e BAZEL_EXTRA_TEST_OPTIONS -e BAZEL_REMOTE_CACHE \
  -e BAZEL_REMOTE_INSTANCE -e GCP_SERVICE_ACCOUNT_KEY -e NUM_CPUS -e ENVOY_RBE \
  -v "$PWD":/source --cap-add SYS_PTRACE --cap-add NET_RAW --cap-add NET_ADMIN "${IMAGE_NAME}":"${IMAGE_ID}" \
  /bin/bash -lc "groupadd --gid $(id -g) -f envoygroup && useradd -o --uid $(id -u) --gid $(id -g) --no-create-home \
  --home-dir /source envoybuild && usermod -a -G pcap envoybuild && su envoybuild -c \"cd source && $*\""
