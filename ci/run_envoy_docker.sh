#!/bin/bash

set -e

# shellcheck source=ci/envoy_build_sha.sh
. "$(dirname "$0")"/envoy_build_sha.sh

function is_windows() {
  [[ "$(uname -s)" == *NT* ]]
}

read -ra ENVOY_DOCKER_OPTIONS <<< "${ENVOY_DOCKER_OPTIONS:-}"

export HTTP_PROXY="${HTTP_PROXY:-${http_proxy:-}}"
export HTTPS_PROXY="${HTTPS_PROXY:-${https_proxy:-}}"
export NO_PROXY="${NO_PROXY:-${no_proxy:-}}"
export GOPROXY="${GOPROXY:-${go_proxy:-}}"

if is_windows; then
  [[ -z "${IMAGE_NAME}" ]] && IMAGE_NAME="envoyproxy/envoy-build-windows2019"
  # TODO(sunjayBhatia): Currently ENVOY_DOCKER_OPTIONS is ignored on Windows because
  # CI sets it to a Linux-specific value. Undo this once https://github.com/envoyproxy/envoy/issues/13272
  # is resolved.
  ENVOY_DOCKER_OPTIONS=()
  # Replace MSYS style drive letter (/c/) with Windows drive letter designation (C:/)
  DEFAULT_ENVOY_DOCKER_BUILD_DIR=$(echo "${TEMP}" | sed -E "s#^/([a-zA-Z])/#\1:/#")/envoy-docker-build
  BUILD_DIR_MOUNT_DEST=C:/build
  SOURCE_DIR=$(echo "${PWD}" | sed -E "s#^/([a-zA-Z])/#\1:/#")
  SOURCE_DIR_MOUNT_DEST=C:/source
  START_COMMAND=("bash" "-c" "cd /c/source && export HOME=/c/build && $*")
else
  [[ -z "${IMAGE_NAME}" ]] && IMAGE_NAME="envoyproxy/envoy-build-ubuntu"
  # We run as root and later drop permissions. This is required to setup the USER
  # in useradd below, which is need for correct Python execution in the Docker
  # environment.
  ENVOY_DOCKER_OPTIONS+=(-u root:root)
  DOCKER_USER_ARGS=()
  DOCKER_GROUP_ARGS=()
  DEFAULT_ENVOY_DOCKER_BUILD_DIR=/tmp/envoy-docker-build
  USER_UID="$(id -u)"
  USER_GID="$(id -g)"
  if [[ -n "$ENVOY_DOCKER_IN_DOCKER" ]]; then
      ENVOY_DOCKER_OPTIONS+=(-v /var/run/docker.sock:/var/run/docker.sock)
      DOCKER_GID="$(stat -c %g /var/run/docker.sock 2>/dev/null || stat -f %g /var/run/docker.sock)"
      DOCKER_USER_ARGS=(--gid "${DOCKER_GID}")
      DOCKER_GROUP_ARGS=(--gid "${DOCKER_GID}")
  else
      DOCKER_GROUP_ARGS+=(--gid "${USER_GID}")
      DOCKER_USER_ARGS+=(--gid "${USER_GID}")
  fi
  BUILD_DIR_MOUNT_DEST=/build
  SOURCE_DIR="${PWD}"
  SOURCE_DIR_MOUNT_DEST=/source
  START_COMMAND=(
      "/bin/bash"
      "-lc"
      "groupadd ${DOCKER_GROUP_ARGS[*]} -f envoygroup \
          && useradd -o --uid ${USER_UID} ${DOCKER_USER_ARGS[*]} --no-create-home --home-dir /build envoybuild \
          && usermod -a -G pcap envoybuild \
          && chown envoybuild:envoygroup /build \
          && chown envoybuild /proc/self/fd/2 \
          && sudo -EHs -u envoybuild bash -c 'cd /source && $*'")
fi

if [[ -n "$ENVOY_DOCKER_PLATFORM" ]]; then
    echo "Setting Docker platform: ${ENVOY_DOCKER_PLATFORM}"
    docker run --rm --privileged multiarch/qemu-user-static --reset -p yes
    ENVOY_DOCKER_OPTIONS+=(--platform "$ENVOY_DOCKER_PLATFORM")
fi

# The IMAGE_ID defaults to the CI hash but can be set to an arbitrary image ID (found with 'docker
# images').
if [[ -z "${IMAGE_ID}" ]]; then
    IMAGE_ID="${ENVOY_BUILD_SHA}"
    if ! is_windows && [[ -n "$ENVOY_BUILD_CONTAINER_SHA" ]]; then
        IMAGE_ID="${ENVOY_BUILD_SHA}@sha256:${ENVOY_BUILD_CONTAINER_SHA}"
    fi
fi
[[ -z "${ENVOY_DOCKER_BUILD_DIR}" ]] && ENVOY_DOCKER_BUILD_DIR="${DEFAULT_ENVOY_DOCKER_BUILD_DIR}"
# Replace backslash with forward slash for Windows style paths
ENVOY_DOCKER_BUILD_DIR="${ENVOY_DOCKER_BUILD_DIR//\\//}"
mkdir -p "${ENVOY_DOCKER_BUILD_DIR}"

[[ -t 1 ]] && ENVOY_DOCKER_OPTIONS+=("-it")
[[ -f .git ]] && [[ ! -d .git ]] && ENVOY_DOCKER_OPTIONS+=(-v "$(git rev-parse --git-common-dir):$(git rev-parse --git-common-dir)")
[[ -n "${SSH_AUTH_SOCK}" ]] && ENVOY_DOCKER_OPTIONS+=(-v "${SSH_AUTH_SOCK}:${SSH_AUTH_SOCK}" -e SSH_AUTH_SOCK)

export ENVOY_BUILD_IMAGE="${IMAGE_NAME}:${IMAGE_ID}"

VOLUMES=(
    -v "${ENVOY_DOCKER_BUILD_DIR}":"${BUILD_DIR_MOUNT_DEST}"
    -v "${SOURCE_DIR}":"${SOURCE_DIR_MOUNT_DEST}")

if ! is_windows; then
    export BUILD_DIR="${BUILD_DIR_MOUNT_DEST}"
fi

if [[ -n "$ENVOY_DOCKER_IN_DOCKER" || -n "$ENVOY_SHARED_TMP_DIR" ]]; then
    # Create a "shared" directory that has the same path in/outside the container
    # This allows the host docker engine to see artefacts using a temporary path created inside the container,
    # at the same path.
    # For example, a directory created with `mktemp -d --tmpdir /tmp/bazel-shared` can be mounted as a volume
    # from within the build container.
    SHARED_TMP_DIR="${ENVOY_SHARED_TMP_DIR:-/tmp/bazel-shared}"
    mkdir -p "${SHARED_TMP_DIR}"
    chmod +rwx "${SHARED_TMP_DIR}"
    VOLUMES+=(-v "${SHARED_TMP_DIR}":"${SHARED_TMP_DIR}")
fi

if [[ -n "${ENVOY_DOCKER_PULL}" ]]; then
    time docker pull "${ENVOY_BUILD_IMAGE}"
fi

# Since we specify an explicit hash, docker-run will pull from the remote repo if missing.
docker run --rm \
       "${ENVOY_DOCKER_OPTIONS[@]}" \
       "${VOLUMES[@]}" \
       -e BUILD_DIR \
       -e HTTP_PROXY \
       -e HTTPS_PROXY \
       -e NO_PROXY \
       -e GOPROXY \
       -e BAZEL_STARTUP_OPTIONS \
       -e BAZEL_BUILD_EXTRA_OPTIONS \
       -e BAZEL_EXTRA_TEST_OPTIONS \
       -e BAZEL_FAKE_SCM_REVISION \
       -e BAZEL_REMOTE_CACHE \
       -e BAZEL_STARTUP_EXTRA_OPTIONS \
       -e CI_BRANCH \
       -e CI_SHA1 \
       -e CI_TARGET_BRANCH \
       -e DOCKERHUB_USERNAME \
       -e DOCKERHUB_PASSWORD \
       -e ENVOY_DOCKER_SAVE_IMAGE \
       -e ENVOY_STDLIB \
       -e BUILD_REASON \
       -e BAZEL_REMOTE_INSTANCE \
       -e GCP_SERVICE_ACCOUNT_KEY \
       -e GCP_SERVICE_ACCOUNT_KEY_PATH \
       -e NUM_CPUS \
       -e ENVOY_BRANCH \
       -e ENVOY_RBE \
       -e ENVOY_BUILD_IMAGE \
       -e ENVOY_SRCDIR \
       -e ENVOY_BUILD_TARGET \
       -e ENVOY_BUILD_DEBUG_INFORMATION \
       -e ENVOY_BUILD_FILTER_EXAMPLE \
       -e ENVOY_COMMIT \
       -e ENVOY_HEAD_REF \
       -e ENVOY_PUBLISH_DRY_RUN \
       -e ENVOY_REPO \
       -e ENVOY_TARBALL_DIR \
       -e SYSTEM_PULLREQUEST_PULLREQUESTNUMBER \
       -e GCS_ARTIFACT_BUCKET \
       -e GITHUB_REF_NAME \
       -e GITHUB_REF_TYPE \
       -e GITHUB_TOKEN \
       -e GITHUB_APP_ID \
       -e GITHUB_INSTALL_ID \
       -e MOBILE_DOCS_CHECKOUT_DIR \
       -e BUILD_SOURCEBRANCHNAME \
       -e BAZELISK_BASE_URL \
       -e ENVOY_BUILD_ARCH \
       -e SYSTEM_STAGEDISPLAYNAME \
       -e SYSTEM_JOBDISPLAYNAME \
       "${ENVOY_BUILD_IMAGE}" \
       "${START_COMMAND[@]}"
