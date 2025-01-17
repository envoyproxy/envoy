#!/bin/bash -e

set -o pipefail

RBE_AUTOCONF_ROOT=$(bazel info workspace)
export RBE_AUTOCONF_ROOT
BAZEL_OUTPUT_BASE=$(bazel info output_base)
BAZEL_VERSION="$(cat .bazelversion)"
CONTAINER_TAG=$(git log -1 --pretty=format:"%H" "${RBE_AUTOCONF_ROOT}/docker")
RBE_CONFIG_GEN_DIR="${BAZEL_OUTPUT_BASE}/external/bazel_toolchains/cmd/rbe_configs_gen"
BAZELRC_DEST="${RBE_AUTOCONF_ROOT}/toolchains/configs/${OS_FAMILY}/.latest.bazelrc"

if [[ "$GCR_IMAGE_NAME" ]]; then
    DOCKER_IMAGE="gcr.io/envoy-ci/${GCR_IMAGE_NAME}:${CONTAINER_TAG}"
elif [[ "$DOCKER_IMAGE" ]]; then
    DOCKER_IMAGE="${DOCKER_IMAGE}:${CONTAINER_PREFIX}${CONTAINER_TAG}${DOCKER_IMAGE_SUFFIX}"
else
    echo "Neither DOCKER_IMAGE nor GCR_IMAGE_NAME set, exiting"
    exit 1
fi

pull_image () {
    echo "Pulling Docker image: ${DOCKER_IMAGE}"
    if ! docker pull -q "${DOCKER_IMAGE}"; then
        echo "Image is not built, skip..."
        exit 0
    fi
}

if [[ -z "$NO_PULL_IMAGE" ]]; then
    pull_image
fi


# If we are committing changes, pull before modifying to ensure no conflicts
if [[ "${COMMIT_TOOLCHAINS}" == "true" ]]; then
    git pull origin refs/heads/main --ff-only
fi

rm -rf "${RBE_AUTOCONF_ROOT}/toolchains/configs/${OS_FAMILY}"
mkdir -p "${RBE_AUTOCONF_ROOT}/toolchains/configs/${OS_FAMILY}"

case "${OS_FAMILY}" in
  linux)
    TOOLCHAIN_LIST=(clang clang_libcxx gcc)
    BAZELRC_LATEST="${RBE_AUTOCONF_ROOT}/toolchains/linux.latest.bazelrc"
    ;;
esac

# Fetch external dependencies
bazel fetch :all

# Build utility for generating RBE config
cd "${RBE_CONFIG_GEN_DIR}" || exit 1
go build
cd - || exit 1

for TOOLCHAIN in "${TOOLCHAIN_LIST[@]}"; do
    echo "Generate toolchain: ${TOOLCHAIN}"
    "${RBE_CONFIG_GEN_DIR}/rbe_configs_gen" \
        -exec_os "${OS_FAMILY}" \
        -generate_java_configs=false \
        -generate_cpp_configs \
        -output_src_root "${RBE_AUTOCONF_ROOT}" \
        -output_config_path "toolchains/configs/${OS_FAMILY}/${TOOLCHAIN}" \
        -target_os "${OS_FAMILY}" \
        -bazel_version "${BAZEL_VERSION}" \
        -toolchain_container "${DOCKER_IMAGE}" \
        -cpp_env_json "${RBE_AUTOCONF_ROOT}/toolchains/${TOOLCHAIN}.env.json"
done

cp "${BAZELRC_LATEST}" "${BAZELRC_DEST}"

chmod -R 755 "${RBE_AUTOCONF_ROOT}/toolchains/configs/${OS_FAMILY}"

git add "${RBE_AUTOCONF_ROOT}/toolchains/configs/${OS_FAMILY}"
