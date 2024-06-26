#!/bin/sh

# Helper shell script for :corpus_from_config_impl genrule in BUILD.

# Set NORUNFILES so test/main doesn't fail when runfiles manifest is not found.
if ! TEXT=$(NORUNFILES=1 "$@" 2>&1); then
   echo "$TEXT"
   echo "Router test failed to pass: debug logs above"
   exit 1
fi

set -e

# Verify at least one entry is actually generated
[ -e "${GENRULE_OUTPUT_DIR}"/generated_corpus_0 ]
