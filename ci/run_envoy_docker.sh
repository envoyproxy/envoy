#!/bin/bash

set -e

. $(dirname $0)/envoy_build_sha.sh

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

[[ -t 1 ]] && ENVOY_DOCKER_OPTIONS+=" -it"
[[ -f .git ]] && [[ ! -d .git ]] && ENVOY_DOCKER_OPTIONS+=" -v $(git rev-parse --git-common-dir):$(git rev-parse --git-common-dir)"

export ENVOY_BUILD_IMAGE="${IMAGE_NAME}:${IMAGE_ID}"

mkdir -p "${ENVOY_DOCKER_BUILD_DIR}"
# Since we specify an explicit hash, docker-run will pull from the remote repo if missing.
docker run --rm ${ENVOY_DOCKER_OPTIONS} -e HTTP_PROXY=${http_proxy} -e HTTPS_PROXY=${https_proxy} -e NO_PROXY=${no_proxy} \
  -u "${USER}":"${USER_GROUP}" -v "${ENVOY_DOCKER_BUILD_DIR}":/build -v /var/run/docker.sock:/var/run/docker.sock  \
  -e BAZEL_BUILD_EXTRA_OPTIONS -e BAZEL_EXTRA_TEST_OPTIONS -e BAZEL_REMOTE_CACHE -e ENVOY_STDLIB -e BUILD_REASON \
  -e BAZEL_REMOTE_INSTANCE -e GCP_SERVICE_ACCOUNT_KEY -e NUM_CPUS -e ENVOY_RBE -e FUZZIT_API_KEY -e ENVOY_BUILD_IMAGE \
  -e ENVOY_SRCDIR -e ENVOY_BUILD_TARGET -e SYSTEM_PULLREQUEST_TARGETBRANCH -e SYSTEM_PULLREQUEST_PULLREQUESTNUMBER \
  -e GCS_ARTIFACT_BUCKET -e BUILD_SOURCEBRANCHNAME -e BAZELISK_BASE_URL \
  -v "$PWD":/source --cap-add SYS_PTRACE --cap-add NET_RAW --cap-add NET_ADMIN "${ENVOY_BUILD_IMAGE}" \
  /bin/bash -lc "groupadd --gid $(id -g) -f envoygroup && useradd -o --uid $(id -u) --gid $(id -g) --no-create-home \
  --home-dir /build envoybuild && usermod -a -G pcap envoybuild && sudo -EHs -u envoybuild bash -c \"cd /source && $*\""
