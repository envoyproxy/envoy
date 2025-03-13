#!/usr/bin/env bash
# This script can be used to run bazel commands. It will attempt to translate paths in compiler
# error messages to real system paths (vs. bazel symbolic links) which may be necessary for some
# IDEs to properly associate error messages to files.
# To invoke, do something like:
# tools/path_fix.sh bazel build //test/...
#
# NOTE: This implementation is far from perfect and will need to be refined to cover all cases.

"$@" 2>&1 |
  while IFS= read -r LINE
  do
    if [[ "${LINE}" =~ [[:space:]]*([^:[:space:]]+):[[:digit:]]+:[[:digit:]]+: ]]; then
      # Bazel now appears to sometimes spit out paths that don't actually exist on disk at all. I
      # have no idea why this is happening (sigh). This check makes it so that if readlink fails we
      # don't attempt to fix the path and just print out what we got.
      if REAL_PATH=$(readlink -f "${BASH_REMATCH[1]}"); then
          LINE=${LINE//${BASH_REMATCH[1]}/${REAL_PATH}}
      fi
    fi
    echo "${LINE}"
  done
