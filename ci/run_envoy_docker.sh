#!/bin/bash

set -e

# shellcheck source=ci/envoy_build_sha.sh
. "$(dirname "$0")"/envoy_build_sha.sh

function is_windows() {
  [[ "$(uname -s)" == *NT* ]]
}

read -ra ENVOY_DOCKER_OPTIONS <<< "${ENVOY_DOCKER_OPTIONS:-}"

# TODO(phlax): uppercase these env vars
export HTTP_PROXY="${http_proxy:-}"
export HTTPS_PROXY="${https_proxy:-}"
export NO_PROXY="${no_proxy:-}"

if is_windows; then
  [[ -z "${IMAGE_NAME}" ]] && IMAGE_NAME="envoyproxy/envoy-build-windows2019"
  # TODO(sunjayBhatia): Currently ENVOY_DOCKER_OPTIONS is ignored on Windows because
  # CI sets it to a Linux-specific value. Undo this once https://github.com/envoyproxy/envoy/issues/13272
  # is resolved.
  ENVOY_DOCKER_OPTIONS=()
  DEFAULT_ENVOY_DOCKER_BUILD_DIR=C:/Windows/Temp/envoy-docker-build
  BUILD_DIR_MOUNT_DEST=C:/build
  # Replace MSYS style drive letter (/c/) with driver letter designation (C:/)
  SOURCE_DIR=$(echo "${PWD}" | sed -E "s#/([a-zA-Z])/#\1:/#")
  SOURCE_DIR_MOUNT_DEST=C:/source
  START_COMMAND=("bash" "-c" "cd source && $*")
else
  [[ -z "${IMAGE_NAME}" ]] && IMAGE_NAME="envoyproxy/envoy-build-ubuntu"
  # We run as root and later drop permissions. This is required to setup the USER
  # in useradd below, which is need for correct Python execution in the Docker
  # environment.
  ENVOY_DOCKER_OPTIONS+=(-u root:root)
  ENVOY_DOCKER_OPTIONS+=(-v /var/run/docker.sock:/var/run/docker.sock)
  ENVOY_DOCKER_OPTIONS+=(--cap-add SYS_PTRACE --cap-add NET_RAW --cap-add NET_ADMIN)
  DEFAULT_ENVOY_DOCKER_BUILD_DIR=/tmp/envoy-docker-build
  BUILD_DIR_MOUNT_DEST=/build
  SOURCE_DIR="${PWD}"
  SOURCE_DIR_MOUNT_DEST=/source
  START_COMMAND=("/bin/bash" "-lc" "groupadd --gid $(id -g) -f envoygroup \
    && useradd -o --uid $(id -u) --gid $(id -g) --no-create-home --home-dir /build envoybuild \
    && usermod -a -G pcap envoybuild \
    && chown envoybuild:envoygroup /build \
    && sudo -EHs -u envoybuild bash -c 'cd /source && $*'")
fi

# The IMAGE_ID defaults to the CI hash but can be set to an arbitrary image ID (found with 'docker
# images').
[[ -z "${IMAGE_ID}" ]] && IMAGE_ID="${ENVOY_BUILD_SHA}"
[[ -z "${ENVOY_DOCKER_BUILD_DIR}" ]] && ENVOY_DOCKER_BUILD_DIR="${DEFAULT_ENVOY_DOCKER_BUILD_DIR}"
# Replace backslash with forward slash for Windows style paths
ENVOY_DOCKER_BUILD_DIR="${ENVOY_DOCKER_BUILD_DIR//\\//}"
mkdir -p "${ENVOY_DOCKER_BUILD_DIR}"

[[ -t 1 ]] && ENVOY_DOCKER_OPTIONS+=("-it")
[[ -f .git ]] && [[ ! -d .git ]] && ENVOY_DOCKER_OPTIONS+=(-v "$(git rev-parse --git-common-dir):$(git rev-parse --git-common-dir)")
[[ -n "${SSH_AUTH_SOCK}" ]] && ENVOY_DOCKER_OPTIONS+=(-v "${SSH_AUTH_SOCK}:${SSH_AUTH_SOCK}" -e SSH_AUTH_SOCK)

export ENVOY_BUILD_IMAGE="${IMAGE_NAME}:${IMAGE_ID}"

# Since we specify an explicit hash, docker-run will pull from the remote repo if missing.
docker run --rm \
       "${ENVOY_DOCKER_OPTIONS[@]}" \
       -v "${ENVOY_DOCKER_BUILD_DIR}":"${BUILD_DIR_MOUNT_DEST}" \
       -v "${SOURCE_DIR}":"${SOURCE_DIR_MOUNT_DEST}" \
       -e AZP_BRANCH \
       -e HTTP_PROXY \
       -e HTTPS_PROXY \
       -e NO_PROXY \
       -e BAZEL_STARTUP_OPTIONS \
       -e BAZEL_BUILD_EXTRA_OPTIONS \
       -e BAZEL_EXTRA_TEST_OPTIONS \
       -e BAZEL_REMOTE_CACHE \
       -e ENVOY_STDLIB \
       -e BUILD_REASON \
       -e BAZEL_REMOTE_INSTANCE \
       -e GCP_SERVICE_ACCOUNT_KEY \
       -e NUM_CPUS \
       -e ENVOY_RBE \
       -e ENVOY_BUILD_IMAGE \
       -e ENVOY_SRCDIR \
       -e ENVOY_BUILD_TARGET \
       -e SYSTEM_PULLREQUEST_PULLREQUESTNUMBER \
       -e GCS_ARTIFACT_BUCKET \
       -e GITHUB_TOKEN \
       -e BUILD_SOURCEBRANCHNAME \
       -e BAZELISK_BASE_URL \
       -e ENVOY_BUILD_ARCH \
       -e SLACK_TOKEN \
       -e BUILD_URI\
       -e REPO_URI \
       -e SYSTEM_STAGEDISPLAYNAME \
       -e SYSTEM_JOBDISPLAYNAME \
       -e SYSTEM_PULLREQUEST_PULLREQUESTID \
       "${ENVOY_BUILD_IMAGE}" \
       "${START_COMMAND[@]}"
