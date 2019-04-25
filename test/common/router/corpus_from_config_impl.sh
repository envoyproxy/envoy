#!/bin/sh

# Helper shell script for :corpus_from_config_impl genrule in BUILD.

set -e

# TEST_SRCDIR/TEST_WORKSPACE don't matter to config_impl_test, but they need to
# be present because main.cc checks for their presence.
TEST_SRCDIR=/totally TEST_WORKSPACE=/bogus $*

# Verify at least one entry is actually generated
[ -e "${GENRULE_OUTPUT_DIR}"/generated_corpus_0 ]
