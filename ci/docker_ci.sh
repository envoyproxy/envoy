#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CI logs.
set -e

function is_windows() {
  [[ "$(uname -s)" == *NT* ]]
}

ENVOY_DOCKER_IMAGE_DIRECTORY="${ENVOY_DOCKER_IMAGE_DIRECTORY:-${BUILD_STAGINGDIRECTORY:-.}/build_images}"

# Setting environments for buildx tools
config_env() {
  # Install QEMU emulators
  docker run --rm --privileged tonistiigi/binfmt --install all

  # Remove older build instance
  docker buildx rm multi-builder || :
  docker buildx create --use --name multi-builder --platform linux/arm64,linux/amd64
}

build_platforms() {
  TYPE=$1
  FILE_SUFFIX="${TYPE/-debug/}"
  FILE_SUFFIX="${FILE_SUFFIX/-contrib/}"

  if is_windows; then
    echo "windows/amd64"
  elif [[ -z "${FILE_SUFFIX}" ]]; then
      echo "linux/arm64,linux/amd64"
  else
      echo "linux/amd64"
  fi
}

build_args() {
  TYPE=$1

  if [[ "${TYPE}" == *-windows* ]]; then
    printf ' -f ci/Dockerfile-envoy-windows --build-arg BUILD_OS=%s --build-arg BUILD_TAG=%s' "${WINDOWS_IMAGE_BASE}" "${WINDOWS_IMAGE_TAG}"
  else
    TARGET="${TYPE/-debug/}"
    TARGET="${TARGET/-contrib/}"
    printf ' -f ci/Dockerfile-envoy --target %s' "envoy${TARGET}"
  fi

  if [[ "${TYPE}" == *-contrib* ]]; then
      printf ' --build-arg ENVOY_BINARY=envoy-contrib'
  fi

  if [[ "${TYPE}" == *-debug ]]; then
      printf ' --build-arg ENVOY_BINARY_SUFFIX='
  fi
}

use_builder() {
  # BuildKit is not available for Windows images, skip this
  if ! is_windows; then
    docker buildx use multi-builder
  fi
}

build_images() {
  local _args args=()
  TYPE=$1
  BUILD_TAG=$2

  use_builder "${TYPE}"
  _args=$(build_args "${TYPE}")
  read -ra args <<< "$_args"
  PLATFORM="$(build_platforms "${TYPE}")"

  if ! is_windows && ! [[ "${TYPE}" =~ debug ]]; then
    args+=("-o" "type=oci,dest=${ENVOY_DOCKER_IMAGE_DIRECTORY}/envoy${TYPE}.tar")
  fi

  docker "${BUILD_COMMAND[@]}" --platform "${PLATFORM}" "${args[@]}" -t "${BUILD_TAG}" .
}

push_images() {
  local _args args=()
  TYPE=$1
  BUILD_TAG=$2

  use_builder "${TYPE}"
  _args=$(build_args "${TYPE}")
  read -ra args <<< "$_args"
  PLATFORM="$(build_platforms "${TYPE}")"
  # docker buildx doesn't do push with default builder
  docker "${BUILD_COMMAND[@]}" --platform "${PLATFORM}" "${args[@]}" -t "${BUILD_TAG}" . --push || \
  docker push "${BUILD_TAG}"
}

MAIN_BRANCH="refs/heads/main"
RELEASE_BRANCH_REGEX="^refs/heads/release/v.*"
RELEASE_TAG_REGEX="^refs/tags/v.*"

# For main builds and release branch builds use the dev repo. Otherwise we assume it's a tag and
# we push to the primary repo.
if [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
  IMAGE_POSTFIX=""
  IMAGE_NAME="${AZP_BRANCH/refs\/tags\//}"
else
  IMAGE_POSTFIX="-dev"
  IMAGE_NAME="${AZP_SHA1}"
fi

# This prefix is altered for the private security images on setec builds.
DOCKER_IMAGE_PREFIX="${DOCKER_IMAGE_PREFIX:-envoyproxy/envoy}"
mkdir -p "${ENVOY_DOCKER_IMAGE_DIRECTORY}"

if is_windows; then
  BUILD_TYPES=("-${WINDOWS_BUILD_TYPE}")
  # BuildKit is not available for Windows images, use standard build command
  BUILD_COMMAND=("build")
else
  # "-google-vrp" must come afer "" to ensure we rebuild the local base image dependency.
  BUILD_TYPES=("" "-debug" "-contrib" "-contrib-debug" "-distroless" "-google-vrp" "-tools")

  # Configure docker-buildx tools
  BUILD_COMMAND=("buildx" "build")
  config_env
fi

# Test the docker build in all cases, but use a local tag that we will overwrite before push in the
# cases where we do push.
for BUILD_TYPE in "${BUILD_TYPES[@]}"; do
    image_tag="${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${IMAGE_NAME}"
    build_images "${BUILD_TYPE}" "$image_tag"
done

# Only push images for main builds, release branch builds, and tag builds.
if [[ "${AZP_BRANCH}" != "${MAIN_BRANCH}" ]] &&
  ! [[ "${AZP_BRANCH}" =~ ${RELEASE_BRANCH_REGEX} ]] &&
  ! [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
  echo 'Ignoring non-main branch or tag for docker push.'
  exit 0
fi

docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"

for BUILD_TYPE in "${BUILD_TYPES[@]}"; do
  push_images "${BUILD_TYPE}" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${IMAGE_NAME}"

  # Only push latest on main builds.
  if [[ "${AZP_BRANCH}" == "${MAIN_BRANCH}" ]]; then
    is_windows && docker tag "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${IMAGE_NAME}" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:latest"
    push_images "${BUILD_TYPE}" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:latest"
  fi

  # Push vX.Y-latest to tag the latest image in a release line
  if [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
    RELEASE_LINE=$(echo "$IMAGE_NAME" | sed -E 's/(v[0-9]+\.[0-9]+)\.[0-9]+/\1-latest/')
    is_windows && docker tag "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${IMAGE_NAME}" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${RELEASE_LINE}"
    push_images "${BUILD_TYPE}" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${RELEASE_LINE}"
  fi
done
