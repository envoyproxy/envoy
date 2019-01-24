#!/bin/bash

# Extracts comments from C++ source code and protobuf definitions. Converts all words to
# lowercase and returns a list of potentially misspelled words, in increasing order of
# frequency. (Low frequency words are more likely to be misspelled.)

TOOLS_DIR=$(cd `dirname $0`; pwd)
ROOT=$(cd "${TOOLS_DIR}/.."; pwd)

CODE_DIRS=("${ROOT}/api" "${ROOT}/include" "${ROOT}/source" "${ROOT}/test")

if ! aspell 2>&1 >/dev/null; then
    echo "comment spell check requires that aspell be installed"
    exit 1
fi

find "${CODE_DIRS[@]}" -type f -print | \
    grep -E "\.(cc|h|proto)$" | \
    xargs "${TOOLS_DIR}/spelling_extract_comments.py" | \
    aspell list | \
    tr '[A-Z]' '[a-z]' | \
    sort | \
    uniq -c | \
    sort -n
