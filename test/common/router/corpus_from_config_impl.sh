#!/bin/sh

# Helper shell script for :corpus_from_config_impl genrule in BUILD.

set -e

# Set NORUNFILES so test/main doesn't fail when runfiles manifest is not found.
NORUNFILES=1 $*

# Verify at least one entry is actually generated
[ -e "${GENRULE_OUTPUT_DIR}"/generated_corpus_0 ]
