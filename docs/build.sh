#!/bin/bash

. tools/shell_utils.sh

set -e

# We need to set ENVOY_DOCS_VERSION_STRING and ENVOY_DOCS_RELEASE_LEVEL for Sphinx.
# We also validate that the tag and version match at this point if needed.
if [ -n "$CIRCLE_TAG" ]
then
  # Check the git tag matches the version number in the VERSION file.
  VERSION_NUMBER=$(cat VERSION)
  if [ "v${VERSION_NUMBER}" != "${CIRCLE_TAG}" ]; then
    echo "Given git tag does not match the VERSION file content:"
    echo "${CIRCLE_TAG} vs $(cat VERSION)"
    exit 1
  fi
  # Check the version_history.rst contains current release version.
  grep --fixed-strings "$VERSION_NUMBER" docs/root/intro/version_history.rst \
    || (echo "Git tag not found in version_history.rst" && exit 1)

  # Now that we now there is a match, we can use the tag.
  export ENVOY_DOCS_VERSION_STRING="tag-$CIRCLE_TAG"
  export ENVOY_DOCS_RELEASE_LEVEL=tagged
  export ENVOY_BLOB_SHA="$CIRCLE_TAG"
else
  BUILD_SHA=$(git rev-parse HEAD)
  VERSION_NUM=$(cat VERSION)
  export ENVOY_DOCS_VERSION_STRING="${VERSION_NUM}"-"${BUILD_SHA:0:6}"
  export ENVOY_DOCS_RELEASE_LEVEL=pre-release
  export ENVOY_BLOB_SHA="$BUILD_SHA"
fi

SCRIPT_DIR=$(dirname "$0")
API_DIR=$(dirname "$dir")/api
BUILD_DIR=build_docs
[[ -z "${DOCS_OUTPUT_DIR}" ]] && DOCS_OUTPUT_DIR=generated/docs
[[ -z "${GENERATED_RST_DIR}" ]] && GENERATED_RST_DIR=generated/rst

rm -rf "${DOCS_OUTPUT_DIR}"
mkdir -p "${DOCS_OUTPUT_DIR}"

rm -rf "${GENERATED_RST_DIR}"
mkdir -p "${GENERATED_RST_DIR}"

source_venv "$BUILD_DIR"
pip3 install -r "${SCRIPT_DIR}"/requirements.txt

# Clean up any stale files in the API tree output. Bazel remembers valid cached
# files still.
rm -rf bazel-bin/external/envoy_api

bazel build ${BAZEL_BUILD_OPTIONS} @envoy_api//docs:protos --aspects \
  tools/protodoc/protodoc.bzl%protodoc_aspect --output_groups=rst --action_env=CPROFILE_ENABLED=1 \
  --action_env=ENVOY_BLOB_SHA --host_force_python=PY3

# We do ** matching below to deal with Bazel cache blah (source proto artifacts
# are nested inside source package targets).
shopt -s globstar

# Find all source protos.
declare -r PROTO_TARGET=$(bazel query "labels(srcs, labels(deps, @envoy_api//docs:protos))")

# Only copy in the protos we care about and know how to deal with in protodoc.
for p in ${PROTO_TARGET}
do
  declare PROTO_FILE_WITHOUT_PREFIX="${p#@envoy_api//}"
  declare PROTO_FILE_CANONICAL="${PROTO_FILE_WITHOUT_PREFIX/://}"
  # We use ** glob matching here to deal with the fact that we have something
  # like
  # bazel-bin/external/envoy_api/envoy/admin/v2alpha/pkg/envoy/admin/v2alpha/certs.proto.proto
  # and we don't want to have to do a nested loop and slow bazel query to
  # recover the canonical package part of the path.
  declare SRCS=(bazel-bin/external/envoy_api/**/"${PROTO_FILE_CANONICAL}.rst")
  # While we may have reformatted the file multiple times due to the transitive
  # dependencies in the aspect above, they all look the same. So, just pick an
  # arbitrary match and we're done.
  declare SRC="${SRCS[0]}"
  declare DST="${GENERATED_RST_DIR}/api-v2/${PROTO_FILE_CANONICAL#envoy/}".rst

  mkdir -p "$(dirname "${DST}")"
  cp -f "${SRC}" "$(dirname "${DST}")"
done

mkdir -p ${GENERATED_RST_DIR}/api-docs

cp -f $API_DIR/xds_protocol.rst "${GENERATED_RST_DIR}/api-docs/xds_protocol.rst"

rsync -rav  $API_DIR/diagrams "${GENERATED_RST_DIR}/api-docs"

rsync -av "${SCRIPT_DIR}"/root/ "${SCRIPT_DIR}"/conf.py "${GENERATED_RST_DIR}"

sphinx-build -W --keep-going -b html "${GENERATED_RST_DIR}" "${DOCS_OUTPUT_DIR}"
