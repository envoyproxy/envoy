#!/bin/bash
# This script can be used to run bazel commands. It will attempt to translate paths in compiler
# error messages to real system paths (vs. bazel symbolic links) which may be necessary for some
# IDEs to properly associate error messages to files.
# To invoke, do something like:
# tools/path_fix.sh bazel build //test/...
#
# NOTE: This implementation is far from perfect and will need to be refined to cover all cases.

set -e

$* 2>&1 |
  while IFS= read -r LINE
  do
    if [[ "${LINE}" =~ [[:space:]]*([^:[:space:]]+):[[:digit:]]+:[[:digit:]]+: ]]; then
      REAL_PATH=$(readlink -f "${BASH_REMATCH[1]}")
      LINE=${LINE//${BASH_REMATCH[1]}/${REAL_PATH}}
    fi
    echo "${LINE}"
  done
