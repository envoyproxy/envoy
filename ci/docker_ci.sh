#!/bin/bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CI logs.
set -e

## DEBUGGING (NB: Set these in your env to avoided unwanted changes)
## Set this to _not_ build/push just print what would be
# DOCKER_CI_DRYRUN=true
#
## Set these to tag/push images to your own repo
# DOCKER_IMAGE_PREFIX=mydocker/repo
# DOCKERHUB_USERNAME=me
# DOCKERHUB_PASSWORD=mypassword
#
## Set these to simulate types of CI run
# AZP_SHA1=MOCKSHA
# AZP_BRANCH=refs/heads/main
# AZP_BRANCH=refs/heads/release/v1.43
# AZP_BRANCH=refs/tags/v1.77.3
##

if [[ -n "$DOCKER_CI_DRYRUN" ]]; then
    AZP_SHA1="${AZP_SHA1:-MOCKSHA}"
fi

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

  echo ">> BUILD: ${BUILD_TAG}"
  echo "> docker ${BUILD_COMMAND[*]} --platform ${PLATFORM} ${args[*]} -t ${BUILD_TAG} ."
  if [[ -n "$DOCKER_CI_DRYRUN" ]]; then
      echo ""
      return
  fi
  echo "..."
  docker "${BUILD_COMMAND[@]}" --platform "${PLATFORM}" "${args[@]}" -t "${BUILD_TAG}" .
  echo ""
}

push_images() {
  local _args args=()
  TYPE=$1
  BUILD_TAG=$2

  use_builder "${TYPE}"
  _args=$(build_args "${TYPE}")
  read -ra args <<< "$_args"
  PLATFORM="$(build_platforms "${TYPE}")"

  echo ">> PUSH: ${BUILD_TAG}"
  echo "> docker ${BUILD_COMMAND[*]} --platform ${PLATFORM} ${args[*]} -t ${BUILD_TAG} . --push ||
    docker push ${BUILD_TAG}"
  if [[ -n "$DOCKER_CI_DRYRUN" ]]; then
      echo ""
      return
  fi
  echo "..."
  # docker buildx doesn't do push with default builder
  docker "${BUILD_COMMAND[@]}" --platform "${PLATFORM}" "${args[@]}" -t "${BUILD_TAG}" . --push ||
    docker push "${BUILD_TAG}"
  echo ""
}

image_tag_name () {
    # envoyproxy/envoy-dev:latest
    local build_type="$1" image_name="$2"
    if [[ -z "$image_name" ]]; then
        image_name="$IMAGE_NAME"
    fi
    echo -n "${DOCKER_IMAGE_PREFIX}${build_type}${IMAGE_POSTFIX}:${image_name}"
}

new_image_tag_name () {
    # envoyproxy/envoy:dev-latest
    local build_type="$1" image_name="$2" image_tag
    parts=()
    if [[ -n "$build_type" ]]; then
        parts+=("${build_type:1}")
    fi
    if [[ -n ${IMAGE_POSTFIX:1} ]]; then
        parts+=("${IMAGE_POSTFIX:1}")
    fi
    if [[ -z "$image_name" ]]; then
        parts+=("$IMAGE_NAME")
    elif [[ "$image_name" != "latest" ]]; then
        parts+=("$IMAGE_NAME")
    fi
    image_tag=$(IFS=- ; echo "${parts[*]}")
    echo -n "${DOCKER_IMAGE_PREFIX}:${image_tag}"
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
  if [[ -z "$DOCKER_CI_DRYRUN" ]]; then
      config_env
  fi
fi

# Test the docker build in all cases, but use a local tag that we will overwrite before push in the
# cases where we do push.
for BUILD_TYPE in "${BUILD_TYPES[@]}"; do
  build_images "${BUILD_TYPE}" "$(image_tag_name "${BUILD_TYPE}")"

  if ! is_windows; then
      build_images "${BUILD_TYPE}" "$(new_image_tag_name "${BUILD_TYPE}")"
  fi
done

# Only push images for main builds, release branch builds, and tag builds.
if [[ "${AZP_BRANCH}" != "${MAIN_BRANCH}" ]] &&
  ! [[ "${AZP_BRANCH}" =~ ${RELEASE_BRANCH_REGEX} ]] &&
  ! [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
  echo 'Ignoring non-main branch or tag for docker push.'
  exit 0
fi

if [[ -z "$DOCKER_CI_DRYRUN" ]]; then
    docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"
fi

for BUILD_TYPE in "${BUILD_TYPES[@]}"; do
  push_images "${BUILD_TYPE}" "$(image_tag_name "${BUILD_TYPE}")"
  if ! is_windows; then
      push_images "${BUILD_TYPE}" "$(new_image_tag_name "${BUILD_TYPE}")"
  fi

  # Only push latest on main builds.
  if [[ "${AZP_BRANCH}" == "${MAIN_BRANCH}" ]]; then
    is_windows && docker tag "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${IMAGE_NAME}" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:latest"
    push_images "${BUILD_TYPE}" "$(image_tag_name "${BUILD_TYPE}" latest)"
    if ! is_windows; then
        push_images "${BUILD_TYPE}" "$(new_image_tag_name "${BUILD_TYPE}" latest)"
    fi
  fi

  # Push vX.Y-latest to tag the latest image in a release line
  if [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
    RELEASE_LINE=$(echo "$IMAGE_NAME" | sed -E 's/(v[0-9]+\.[0-9]+)\.[0-9]+/\1-latest/')
    is_windows && docker tag "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${IMAGE_NAME}" "${DOCKER_IMAGE_PREFIX}${BUILD_TYPE}${IMAGE_POSTFIX}:${RELEASE_LINE}"
    push_images "${BUILD_TYPE}" "$(image_tag_name "${BUILD_TYPE}" "${RELEASE_LINE}")"
    if ! is_windows; then
        push_images "${BUILD_TYPE}" "$(new_image_tag_name "${BUILD_TYPE}" "${RELEASE_LINE}")"
    fi
  fi
done
