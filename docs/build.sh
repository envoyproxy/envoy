#!/usr/bin/env bash

# set SPHINX_SKIP_CONFIG_VALIDATION environment variable to true to skip
# validation of configuration examples

. tools/shell_utils.sh

set -e

RELEASE_TAG_REGEX="^refs/tags/v.*"

if [[ "${AZP_BRANCH}" =~ ${RELEASE_TAG_REGEX} ]]; then
  DOCS_TAG="${AZP_BRANCH/refs\/tags\//}"
fi

# We need to set ENVOY_DOCS_VERSION_STRING and ENVOY_DOCS_RELEASE_LEVEL for Sphinx.
# We also validate that the tag and version match at this point if needed.
if [[ -n "${DOCS_TAG}" ]]; then
  # Check the git tag matches the version number in the VERSION file.
  VERSION_NUMBER=$(cat VERSION)
  if [[ "v${VERSION_NUMBER}" != "${DOCS_TAG}" ]]; then
    echo "Given git tag does not match the VERSION file content:"
    echo "${DOCS_TAG} vs $(cat VERSION)"
    exit 1
  fi
  # Check the version_history.rst contains current release version.
  grep --fixed-strings "$VERSION_NUMBER" docs/root/version_history/current.rst \
    || (echo "Git tag not found in version_history/current.rst" && exit 1)

  # Now that we know there is a match, we can use the tag.
  export ENVOY_DOCS_VERSION_STRING="tag-${DOCS_TAG}"
  export ENVOY_DOCS_RELEASE_LEVEL=tagged
  export ENVOY_BLOB_SHA="${DOCS_TAG}"
else
  BUILD_SHA=$(git rev-parse HEAD)
  VERSION_NUM=$(cat VERSION)
  export ENVOY_DOCS_VERSION_STRING="${VERSION_NUM}"-"${BUILD_SHA:0:6}"
  export ENVOY_DOCS_RELEASE_LEVEL=pre-release
  export ENVOY_BLOB_SHA="$BUILD_SHA"
fi

SCRIPT_DIR="$(dirname "$0")"
SRC_DIR="$(dirname "$SCRIPT_DIR")"
API_DIR="${SRC_DIR}"/api
CONFIGS_DIR="${SRC_DIR}"/configs
BUILD_DIR=build_docs
[[ -z "${DOCS_OUTPUT_DIR}" ]] && DOCS_OUTPUT_DIR=generated/docs
[[ -z "${GENERATED_RST_DIR}" ]] && GENERATED_RST_DIR=generated/rst

rm -rf "${DOCS_OUTPUT_DIR}"
mkdir -p "${DOCS_OUTPUT_DIR}"

rm -rf "${GENERATED_RST_DIR}"
mkdir -p "${GENERATED_RST_DIR}"

source_venv "$BUILD_DIR"
pip3 install --require-hashes -r "${SCRIPT_DIR}"/requirements.txt

# Clean up any stale files in the API tree output. Bazel remembers valid cached
# files still.
rm -rf bazel-bin/external/envoy_api_canonical

EXTENSION_DB_PATH="$(realpath "${BUILD_DIR}/extension_db.json")"
export EXTENSION_DB_PATH

# This is for local RBE setup, should be no-op for builds without RBE setting in bazelrc files.
IFS=" " read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTIONS:-}"
BAZEL_BUILD_OPTIONS+=(
    "--remote_download_outputs=all"
    "--strategy=protodoc=sandboxed,local"
    "--action_env=ENVOY_BLOB_SHA"
    "--action_env=EXTENSION_DB_PATH")

# Generate extension database. This maps from extension name to extension
# metadata, based on the envoy_cc_extension() Bazel target attributes.
./docs/generate_extension_db.py "${EXTENSION_DB_PATH}"

# Generate RST for the lists of trusted/untrusted extensions in
# intro/arch_overview/security docs.
mkdir -p "${GENERATED_RST_DIR}"/intro/arch_overview/security
./docs/generate_extension_rst.py "${EXTENSION_DB_PATH}" "${GENERATED_RST_DIR}"/intro/arch_overview/security

# Generate RST for external dependency docs in intro/arch_overview/security.
PYTHONPATH=. ./docs/generate_external_dep_rst.py "${GENERATED_RST_DIR}"/intro/arch_overview/security

function generate_api_rst() {
  local proto_target
  declare -r API_VERSION=$1
  echo "Generating ${API_VERSION} API RST..."

  # Generate the extensions docs
  bazel build "${BAZEL_BUILD_OPTIONS[@]}" @envoy_api_canonical//:"${API_VERSION}"_protos --aspects \
    tools/protodoc/protodoc.bzl%protodoc_aspect --output_groups=rst

  # Fill in boiler plate for extensions that have google.protobuf.Empty as their
  # config.
  bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/protodoc:generate_empty \
    "${PWD}"/docs/empty_extensions.json "${PWD}/${GENERATED_RST_DIR}/api-${API_VERSION}"/config

  # We do ** matching below to deal with Bazel cache blah (source proto artifacts
  # are nested inside source package targets).
  shopt -s globstar

  # Find all source protos.
  proto_target=$(bazel query "labels(srcs, labels(deps, @envoy_api_canonical//:${API_VERSION}_protos))")
  declare -r proto_target

  # Only copy in the protos we care about and know how to deal with in protodoc.
  for p in ${proto_target}
  do
    declare PROTO_FILE_WITHOUT_PREFIX="${p#@envoy_api_canonical//}"
    declare PROTO_FILE_CANONICAL="${PROTO_FILE_WITHOUT_PREFIX/://}"
    # We use ** glob matching here to deal with the fact that we have something
    # like
    # bazel-bin/external/envoy_api_canonical/envoy/admin/v2alpha/pkg/envoy/admin/v2alpha/certs.proto.proto
    # and we don't want to have to do a nested loop and slow bazel query to
    # recover the canonical package part of the path.
    declare SRCS=(bazel-bin/external/envoy_api_canonical/**/"${PROTO_FILE_CANONICAL}.rst")
    # While we may have reformatted the file multiple times due to the transitive
    # dependencies in the aspect above, they all look the same. So, just pick an
    # arbitrary match and we're done.
    declare SRC="${SRCS[0]}"
    declare DST="${GENERATED_RST_DIR}/api-${API_VERSION}/${PROTO_FILE_CANONICAL#envoy/}".rst

    mkdir -p "$(dirname "${DST}")"
    cp -f "${SRC}" "$(dirname "${DST}")"
  done
}

generate_api_rst v2
generate_api_rst v3

# Fixup anchors and references in v3 so they form a distinct namespace.
# TODO(htuch): Do this in protodoc generation in the future.
find "${GENERATED_RST_DIR}"/api-v3 -name "*.rst" -print0 | xargs -0 sed -i -e "s#envoy_api_#envoy_v3_api_#g"
find "${GENERATED_RST_DIR}"/api-v3 -name "*.rst" -print0 | xargs -0 sed -i -e "s#config_resource_monitors#v3_config_resource_monitors#g"

# xDS protocol spec.
mkdir -p ${GENERATED_RST_DIR}/api-docs
cp -f "${API_DIR}"/xds_protocol.rst "${GENERATED_RST_DIR}/api-docs/xds_protocol.rst"
# Edge hardening example YAML.
mkdir -p "${GENERATED_RST_DIR}"/configuration/best_practices
cp -f "${CONFIGS_DIR}"/google-vrp/envoy-edge.yaml "${GENERATED_RST_DIR}"/configuration/best_practices

copy_example_configs () {
    mkdir -p "${GENERATED_RST_DIR}/start/sandboxes/_include"
    cp -a "${SRC_DIR}"/examples/* "${GENERATED_RST_DIR}/start/sandboxes/_include"
}

copy_example_configs

rsync -rav  "${API_DIR}/diagrams" "${GENERATED_RST_DIR}/api-docs"

rsync -av \
      "${SCRIPT_DIR}"/root/ \
      "${SCRIPT_DIR}"/conf.py \
      "${SCRIPT_DIR}"/redirects.txt \
      "${SCRIPT_DIR}"/_ext \
      "${GENERATED_RST_DIR}"

# To speed up validate_fragment invocations in validating_code_block
bazel build "${BAZEL_BUILD_OPTIONS[@]}" //tools/config_validation:validate_fragment

sphinx-build -W --keep-going -b html "${GENERATED_RST_DIR}" "${DOCS_OUTPUT_DIR}"
