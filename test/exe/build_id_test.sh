#!/bin/bash

set -e -o pipefail

if [[ `uname` == "Darwin" ]]; then
  BUILDID=$(otool -X -s __TEXT __build_id source/exe/envoy-static | grep -v section | cut -f2 | xxd -r -p)
else
  BUILDID=$(file -L source/exe/envoy-static | sed -n -E 's/.*BuildID\[sha1\]=([0-9a-f]{40}).*/\1/p')
fi

EXPECTED=$(cat bazel/raw_build_id.ldscript)

if [[ ${BUILDID} != ${EXPECTED} ]]; then
  echo "Build ID mismatch, got: ${BUILDID}, expected: ${EXPECTED}".
  exit 1
fi
