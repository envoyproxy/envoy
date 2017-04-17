#!/bin/bash

# TOOD(htuch): This is now just for run_envoy_tests.sh, remove when no more cmake.

JSON=$1
SRC_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/${JSON}"
DST_FILE="${TEST_TMPDIR}"/"$(basename "${JSON}")"

mkdir -p "$(dirname "${DST_FILE}")"

cat "${SRC_FILE}" | \
  sed -e "s#{{ test_tmpdir }}#$TEST_TMPDIR#g" | \
  sed -e "s#{{ test_udsdir }}#$TEST_UDSDIR#g" | \
  cat > "${DST_FILE}"
