#!/usr/bin/env bash

# Do not ever set -x here, it is a security hazard as it will place the credentials below in the
# CI logs.
set -e

# Workaround for https://github.com/envoyproxy/envoy/issues/26634
DOCKER_BUILD_TIMEOUT="${DOCKER_BUILD_TIMEOUT:-500}"

# Allow single platform builds via DOCKER_BUILD_PLATFORM
if [[ -n "$DOCKER_BUILD_PLATFORM" ]]; then
    DOCKER_PLATFORM="${DOCKER_BUILD_PLATFORM}"
else
    DOCKER_PLATFORM="${DOCKER_PLATFORM:-linux/arm64,linux/amd64}"
fi

if [[ -n "$DOCKER_CI_DRYRUN" ]]; then
    CI_SHA1="${CI_SHA1:-MOCKSHA}"
fi

DEV_VERSION_REGEX="-dev$"
if [[ -z "$ENVOY_VERSION" ]]; then
    ENVOY_VERSION="$(cat VERSION.txt)"
fi

if [[ "$ENVOY_VERSION" =~ $DEV_VERSION_REGEX ]]; then
    # Dev version
    IMAGE_POSTFIX="-dev"
    IMAGE_NAME="${CI_SHA1}"
else
    # Non-dev version
    IMAGE_POSTFIX=""
    IMAGE_NAME="v${ENVOY_VERSION}"
fi

if [[ -n "$DOCKER_LOAD_IMAGES" ]]; then
    LOAD_IMAGES=1
fi

ENVOY_OCI_DIR="${ENVOY_OCI_DIR:-${BUILD_DIR:-.}/build_images}"

# This prefix is altered for the private security images on setec builds.
DOCKER_IMAGE_PREFIX="${DOCKER_IMAGE_PREFIX:-envoyproxy/envoy}"
if [[ -z "$DOCKER_CI_DRYRUN" ]]; then
    mkdir -p "${ENVOY_OCI_DIR}"
fi

# Setting environments for buildx tools

config_env() {
    BUILDKIT_VERSION=$(grep '^FROM moby/buildkit:' ci/Dockerfile-buildkit | cut -d ':' -f2)
    echo ">> BUILDX: install ${BUILDKIT_VERSION}"

    if [[ "${DOCKER_PLATFORM}" == *","*  ]]; then
        echo "> docker run --rm --privileged tonistiigi/binfmt --install all"
        docker run --rm --privileged tonistiigi/binfmt:qemu-v7.0.0 --install all
    fi

    echo "> docker buildx rm envoy-builder 2> /dev/null || :"
    echo "> docker buildx create --use --name envoy-builder --platform ${DOCKER_PLATFORM}"

    # Remove older build instance
    docker buildx rm envoy-builder 2> /dev/null || :
    docker buildx create --use --name envoy-builder --platform "${DOCKER_PLATFORM}" --driver-opt "image=moby/buildkit:${BUILDKIT_VERSION}"
}

BUILD_TYPES=("" "-debug" "-contrib" "-contrib-debug" "-contrib-distroless" "-distroless" "-tools")

if [[ "$DOCKER_PLATFORM" == "linux/amd64" ]]; then
    BUILD_TYPES+=("-google-vrp")
fi

# Configure docker-buildx tools
BUILD_COMMAND=("buildx" "build")
config_env

image_tag_name () {
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
        parts+=("$image_name")
    fi
    image_tag=$(IFS=- ; echo "${parts[*]}")
    echo -n "${DOCKER_IMAGE_PREFIX}:${image_tag}"
}

build_args() {
    local build_type=$1 target

    target="${build_type/-debug/}"
    target="${target/-contrib/}"
    printf ' -f distribution/docker/Dockerfile-envoy --target %s' "envoy${target}"

    if [[ "${build_type}" == *-contrib* ]]; then
        printf ' --build-arg ENVOY_BINARY=envoy-contrib'
    fi

    if [[ "${build_type}" == *-debug ]]; then
        printf ' --build-arg ENVOY_BINARY_PREFIX=dbg/'
    fi
}

use_builder() {
    if [[ "${DOCKER_PLATFORM}" != *","*  ]]; then
        return
    fi
    echo ">> BUILDX: use envoy-builder"
    echo "> docker buildx use envoy-builder"

    if [[ -n "$DOCKER_CI_DRYRUN" ]]; then
        return
    fi
    docker buildx use envoy-builder
}

build_image () {
    local image_type="$1" platform docker_build_args _args args=() docker_build_args docker_image_tarball build_tag action platform size

    action="BUILD"
    use_builder "${image_type}"
    _args=$(build_args "${image_type}")
    read -ra args <<<"$_args"
    platform="$DOCKER_PLATFORM"
    build_tag="$(image_tag_name "${image_type}")"
    docker_image_tarball="${ENVOY_OCI_DIR}/envoy${image_type}.tar"

    # `--sbom` and `--provenance` args added for skopeo 1.5.0 compat,
    # can probably be removed for later versions.
    args+=(
        "--sbom=false"
        "--provenance=false")
    if [[ -n "$LOAD_IMAGES" ]]; then
        action="BUILD+LOAD"
        args+=("--load")
    else
        if [[ "$platform" != *","* ]]; then
            arch="$(echo "$platform" | cut -d/ -f2)"
            docker_image_tarball="${ENVOY_OCI_DIR}/envoy${image_type}.${arch}.tar"
        fi
        action="BUILD+OCI"
        args+=("-o" "type=oci,dest=${docker_image_tarball}")
    fi

    docker_build_args=(
        "${BUILD_COMMAND[@]}"
        "--platform" "${platform}"
        "${args[@]}"
        -t "${build_tag}"
        .)
    echo ">> ${action}: ${build_tag}"
    echo "> docker ${docker_build_args[*]}"

    timeout "$DOCKER_BUILD_TIMEOUT" docker "${docker_build_args[@]}" || {
        if [[ "$?" == 124 ]]; then
            echo "Docker build timed out ..." >&2
        else
            echo "Docker build errored ..." >&2
        fi
        sleep 5
        echo "trying again ..." >&2
        docker "${docker_build_args[@]}"
    }
    if [[ -n "$LOAD_IMAGES" || ! -f "${docker_image_tarball}" ]]; then
        return
    fi
    size=$(du -h "${docker_image_tarball}" | cut -f1)
    echo ">> OCI tarball created: ${docker_image_tarball} (${size})"
}

do_docker_ci () {
    local build_type
    echo "Docker build configuration:"
    echo "  Platform(s): ${DOCKER_PLATFORM}"
    echo "  Output directory: ${ENVOY_OCI_DIR}"
    if [[ -n "$DOCKER_FORCE_OCI_OUTPUT" ]]; then
        echo "  Output format: OCI tarballs only"
    fi
    echo
    for build_type in "${BUILD_TYPES[@]}"; do
        build_image "$build_type"
    done
}

do_docker_ci
