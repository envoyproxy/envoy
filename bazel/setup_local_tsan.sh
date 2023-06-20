#!/bin/bash

BAZELRC_FILE="${BAZELRC_FILE:-$(bazel info workspace)/local_tsan.bazelrc}"

LIBCXX_PREFIX=$1

if [[ ! -e "${LIBCXX_PREFIX}/lib" ]]; then
  echo "Error: cannot find /lib in ${LIBCXX_PREFIX}."
  exit 1
fi


echo "# Generated file, do not edit. Delete this file if you no longer use local tsan-instrumented libc++
build:local-tsan --config=libc++
build:local-tsan --config=clang-tsan
build:local-tsan --linkopt=-L${LIBCXX_PREFIX}/lib
build:local-tsan --linkopt=-Wl,-rpath,${LIBCXX_PREFIX}/lib
" > "${BAZELRC_FILE}"
