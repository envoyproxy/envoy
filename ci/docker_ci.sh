#!/usr/bin/env bash

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
# CI_SHA1=MOCKSHA
# CI_BRANCH=refs/heads/main
# CI_BRANCH=refs/heads/release/v1.43
# CI_BRANCH=refs/tags/v1.77.3
##

# Workaround for https://github.com/envoyproxy/envoy/issues/26634
DOCKER_BUILD_TIMEOUT="${DOCKER_BUILD_TIMEOUT:-500}"

DOCKER_PLATFORM="${DOCKER_PLATFORM:-linux/arm64,linux/amd64}"

if [[ -n "$DOCKER_CI_DRYRUN" ]]; then
    CI_SHA1="${CI_SHA1:-MOCKSHA}"
fi

MAIN_BRANCH="refs/heads/main"
RELEASE_BRANCH_REGEX="^refs/heads/release/v.*"
DEV_VERSION_REGEX="-dev$"
DOCKER_REGISTRY="${DOCKER_REGISTRY:-docker.io}"
PUSH_IMAGES_TO_REGISTRY=
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

# Only push images for main builds, and non-dev release branch builds
if [[ -n "$DOCKER_LOAD_IMAGES" ]]; then
    LOAD_IMAGES=1
elif [[ -n "$DOCKERHUB_USERNAME" ]] && [[ -n "$DOCKERHUB_PASSWORD" ]]; then
    if [[ "${CI_BRANCH}" == "${MAIN_BRANCH}" ]]; then
        echo "Pushing images for main."
        PUSH_IMAGES_TO_REGISTRY=1
    elif [[ "${CI_BRANCH}" =~ ${RELEASE_BRANCH_REGEX} ]] && ! [[ "$ENVOY_VERSION" =~ $DEV_VERSION_REGEX ]]; then
        echo "Pushing images for release branch ${CI_BRANCH}."
        PUSH_IMAGES_TO_REGISTRY=1
    else
        echo 'Ignoring non-release branch for docker push.'
    fi
else
    echo 'No credentials for docker push.'
fi

ENVOY_DOCKER_IMAGE_DIRECTORY="${ENVOY_DOCKER_IMAGE_DIRECTORY:-${BUILD_DIR:-.}/build_images}"
# This prefix is altered for the private security images on setec builds.
DOCKER_IMAGE_PREFIX="${DOCKER_IMAGE_PREFIX:-envoyproxy/envoy}"
if [[ -z "$DOCKER_CI_DRYRUN" ]]; then
    mkdir -p "${ENVOY_DOCKER_IMAGE_DIRECTORY}"
fi

# Setting environments for buildx tools
config_env() {
    echo ">> BUILDX: install"
    echo "> docker run --rm --privileged tonistiigi/binfmt --install all"
    echo "> docker buildx rm multi-builder 2> /dev/null || :"
    echo "> docker buildx create --use --name multi-builder --platform ${DOCKER_PLATFORM}"

    if [[ -n "$DOCKER_CI_DRYRUN" ]]; then
        return
    fi

    # Install QEMU emulators
    docker run --rm --privileged tonistiigi/binfmt --install all

    # Remove older build instance
    docker buildx rm multi-builder 2> /dev/null || :
    docker buildx create --use --name multi-builder --platform "${DOCKER_PLATFORM}"
}

# "-google-vrp" must come afer "" to ensure we rebuild the local base image dependency.
BUILD_TYPES=("" "-debug" "-contrib" "-contrib-debug" "-distroless" "-google-vrp" "-tools")

# Configure docker-buildx tools
BUILD_COMMAND=("buildx" "build")
config_env

old_image_tag_name () {
    # envoyproxy/envoy-dev:latest
    # envoyproxy/envoy-debug:v1.73.3
    # envoyproxy/envoy-debug:v1.73-latest
    local build_type="$1" image_name="$2"
    if [[ -z "$image_name" ]]; then
        image_name="$IMAGE_NAME"
    fi
    echo -n "${DOCKER_IMAGE_PREFIX}${build_type}${IMAGE_POSTFIX}:${image_name}"
}

new_image_tag_name () {
    # envoyproxy/envoy:dev
    # envoyproxy/envoy:debug-v1.73.3
    # envoyproxy/envoy:debug-v1.73-latest

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

build_platforms() {
    local build_type=$1

    if [[ "${build_type}" == *-google-vrp ]]; then
        echo -n "linux/amd64"
    else
        echo -n "$DOCKER_PLATFORM"
    fi
}

build_args() {
    local build_type=$1 target

    target="${build_type/-debug/}"
    target="${target/-contrib/}"
    printf ' -f ci/Dockerfile-envoy --target %s' "envoy${target}"

    if [[ "${build_type}" == *-contrib* ]]; then
        printf ' --build-arg ENVOY_BINARY=envoy-contrib'
    fi

    if [[ "${build_type}" == *-debug ]]; then
        printf ' --build-arg ENVOY_BINARY_PREFIX=dbg/'
    fi
}

use_builder() {
    echo ">> BUILDX: use multi-builder"
    echo "> docker buildx use multi-builder"

    if [[ -n "$DOCKER_CI_DRYRUN" ]]; then
        return
    fi
    docker buildx use multi-builder
}

build_and_maybe_push_image () {
    # If the image is not required for local testing and this is main or a release this will push immediately
    # If it is required for testing on a main or release branch (ie non-debug) it will push to a tar archive
    # and then push to the registry from there.
    local image_type="$1" platform docker_build_args _args args=() docker_build_args docker_image_tarball build_tag action platform

    action="BUILD"
    use_builder "${image_type}"
    _args=$(build_args "${image_type}")
    read -ra args <<<"$_args"
    platform="$(build_platforms "${image_type}")"
    build_tag="$(old_image_tag_name "${image_type}")"
    docker_image_tarball="${ENVOY_DOCKER_IMAGE_DIRECTORY}/envoy${image_type}.tar"

    # `--sbom` and `--provenance` args added for skopeo 1.5.0 compat,
    # can probably be removed for later versions.
    args+=(
        "--sbom=false"
        "--provenance=false")
    if [[ -n "$LOAD_IMAGES" ]]; then
        action="BUILD+LOAD"
        args+=("--load")
    elif [[ "${image_type}" =~ debug ]]; then
        # For linux if its the debug image then push immediately for release branches,
        # otherwise just test the build
        if [[ -n "$PUSH_IMAGES_TO_REGISTRY" ]]; then
            action="BUILD+PUSH"
            args+=("--push")
        fi
    else
        # For linux non-debug builds, save it first in the tarball, we will push it
        # with skopeo from there if needed.
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

    if [[ -z "$DOCKER_CI_DRYRUN" ]]; then
        echo "..."
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
    fi
    if [[ -z "$PUSH_IMAGES_TO_REGISTRY" ]]; then
        return
    fi

    if ! [[ "${image_type}" =~ debug ]]; then
        push_image_from_tarball "$build_tag" "$docker_image_tarball"
    fi
}

tag_image () {
    local build_tag="$1" tag="$2" docker_tag_args

    if [[ "$build_tag" == "$tag" ]]; then
        return
    fi

    echo ">> TAG: ${build_tag} -> ${tag}"

    docker_tag_args=(
        buildx imagetools create
        "${DOCKER_REGISTRY}/${build_tag}"
        "--tag" "${DOCKER_REGISTRY}/${tag}")

    echo "> docker ${docker_tag_args[*]}"

    if [[ -z "$DOCKER_CI_DRYRUN" ]]; then
        echo "..."
        docker "${docker_tag_args[@]}" || {
            echo "Retry Docker tag in 5s ..." >&2
            sleep 5
            docker "${docker_tag_args[@]}"
        }
    fi
}

push_image_from_tarball () {
    # Use skopeo to push from the created oci archive

    local build_tag="$1" docker_image_tarball="$2" src dest

    src="oci-archive:${docker_image_tarball}"
    dest="docker://${DOCKER_REGISTRY}/${build_tag}"
    # dest="oci-archive:${docker_image_tarball2}"

    echo ">> PUSH: ${src} -> ${dest}"
    echo "> skopeo copy --all ${src} ${dest}"

    if [[ -n "$DOCKER_CI_DRYRUN" ]]; then
        return
    fi

    # NB: this command works with skopeo 1.5.0, later versions may require
    #   different flags, eg `--multi-arch all`
    skopeo copy --all "${src}" "${dest}"

    # Test specific versions using a container, eg
    # docker run -v "${HOME}/.docker:/root/.docker" -v "${PWD}/build_images:/build_images" --rm -it \
    #    quay.io/skopeo/stable:v1.5.0 copy --all "${src}" "${dest}"
}

tag_variants () {
    # Tag image variants
    local image_type="$1" build_tag new_image_name release_line variant_type tag_name new_tag_name

    if [[ -z "$PUSH_IMAGES_TO_REGISTRY" ]]; then
        return
    fi

    build_tag="$(old_image_tag_name "${image_type}")"
    new_image_name="$(new_image_tag_name "${image_type}")"

    if [[ "$build_tag" != "$new_image_name" ]]; then
        tag_image "${build_tag}" "${new_image_name}"
    fi

    # Only push latest on main/dev builds.
    if [[ "$ENVOY_VERSION" =~ $DEV_VERSION_REGEX ]]; then
        if [[ "${CI_BRANCH}" == "${MAIN_BRANCH}" ]]; then
            variant_type="latest"
        fi
    else
        # Push vX.Y-latest to tag the latest image in a release line
        release_line="$(echo "$ENVOY_VERSION" | sed -E 's/([0-9]+\.[0-9]+)\.[0-9]+/\1-latest/')"
        variant_type="v${release_line}"
    fi
    if [[ -n "$variant_type" ]]; then
        tag_name="$(old_image_tag_name "${image_type}" "${variant_type}")"
        new_tag_name="$(new_image_tag_name "${image_type}" "${variant_type}")"
        tag_image "${build_tag}" "${tag_name}"
        if [[ "$tag_name" != "$new_tag_name" ]]; then
            tag_image "${build_tag}" "${new_tag_name}"
        fi
    fi
}

build_and_maybe_push_image_and_variants () {
    local image_type="$1"

    build_and_maybe_push_image "$image_type"
    tag_variants "$image_type"

    # Leave blank line before next build
    echo
}

login_docker () {
    echo ">> LOGIN"
    if [[ -z "$DOCKER_CI_DRYRUN" ]]; then
       docker login -u "$DOCKERHUB_USERNAME" -p "$DOCKERHUB_PASSWORD"
    fi
}

do_docker_ci () {
    local build_type

    if [[ -n "$PUSH_IMAGES_TO_REGISTRY" ]]; then
        login_docker
    fi

    for build_type in "${BUILD_TYPES[@]}"; do
        build_and_maybe_push_image_and_variants "${build_type}"
    done
}

do_docker_ci
