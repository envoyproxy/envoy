#!/bin/bash

JSON=$1
SRC_FILE="${TEST_SRCDIR}/${TEST_WORKSPACE}/${JSON}"
DST_FILE="${TEST_TMPDIR}/${JSON}"

mkdir -p "$(dirname "${DST_FILE}")"

cat "${SRC_FILE}" | \
  sed -e "s#{{ test_tmpdir }}#$TEST_TMPDIR#g" | \
  sed -e "s#{{ test_udsdir }}#$TEST_UDSDIR#g" | \
  cat > "${DST_FILE}"
