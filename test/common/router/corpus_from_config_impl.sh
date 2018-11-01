#!/bin/sh

# Helper shell script for :corpus_from_config_impl genrule in BUILD.

set -e

# TEST_SRCDIR/TEST_WORKSPACE don't matter to config_impl_test, but they need to
# be present because main.cc checks for their presence.
TEST_SRCDIR=/totally TEST_WORKSPACE=/bogus $*

# Verify at least one entry is actually generated
[ -e "${ROUTE_CORPUS_PATH}"/generated_corpus_0 ]

# Touch the remaining files so that Bazel doesn't complain they are missing.
for n in $(seq "${ROUTE_CORPUS_MAX}")
do
  touch "${ROUTE_CORPUS_PATH}/generated_corpus_$n"
done
