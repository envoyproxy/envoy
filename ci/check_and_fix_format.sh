#!/usr/bin/env bash

set -e

read -ra BAZEL_STARTUP_OPTIONS <<< "${BAZEL_STARTUP_OPTION_LIST:-}"
read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTION_LIST:-}"

DIFF_OUTPUT="${DIFF_OUTPUT:-/build/fix_proto_format.diff}"
ENVOY_SRCDIR="${ENVOY_SRCDIR:-${PWD}}"

# We set this for two reasons. First, we want to ensure belt-and-braces that we check these formats
# in CI in case the skip-on-file-change heuristics in proto_format.sh etc. are buggy. Second, this
# prevents AZP cache weirdness.
export FORCE_PROTO_FORMAT=yes
export FORCE_PYTHON_FORMAT=yes

function fix {
  set +e
  "${ENVOY_SRCDIR}/tools/proto_format/proto_format.sh" fix
  echo "Format check failed, try apply following patch to fix:"
  bazel "${BAZEL_STARTUP_OPTIONS[@]}" run "${BAZEL_BUILD_OPTIONS[@]}" \
      //tools/git:git -- add api
  bazel "${BAZEL_STARTUP_OPTIONS[@]}" run "${BAZEL_BUILD_OPTIONS[@]}" \
      //tools/git:git -- diff HEAD \
      | tee "${DIFF_OUTPUT}"

  exit 1
}

# If any of the checks fail, run the fix function above.
trap fix ERR

"${ENVOY_SRCDIR}/tools/proto_format/proto_format.sh" check
