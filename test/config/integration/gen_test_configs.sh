#!/bin/bash
#
# Eventually this will fill in port info and temp paths for the test JSON configs. Today, it just
# does a simple sed to handle the test temp path.

set -e

CONFIG_IN_DIR=$1
CONFIG_OUT_DIR=$2

mkdir -p "${CONFIG_OUT_DIR}"

for f in $(find "${CONFIG_IN_DIR}" -name "*.json");
do
  cat $f | sed -e "s#{{ test_tmpdir }}#$TEST_TMPDIR#g" | \
    cat > "${CONFIG_OUT_DIR}"/"$(basename "$f")"
done
