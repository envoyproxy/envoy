#!/bin/bash

set -e

MAIN_BRANCH="refs/heads/master"

# shellcheck source=ci/setup_cache.sh
. "$(dirname "$0")"/setup_cache.sh

if [[ "${AZP_BRANCH}" == "${MAIN_BRANCH}" ]]; then
  BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_EXTRA_OPTIONS}" tools/api/generate_go_protobuf.py
fi
