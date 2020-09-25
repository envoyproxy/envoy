#!/bin/bash

set -ex

TEST_BINARY=$1
shift

# Clear existing corpus if previous run wasn't in sandbox
rm -rf fuzz_corpus

mkdir -p fuzz_corpus/seed_corpus
cp -r $@ fuzz_corpus/seed_corpus

# TODO(asraa): When fuzz targets are stable, remove error suppression and run coverage while fuzzing.
LLVM_PROFILE_FILE= ${TEST_BINARY} fuzz_corpus -seed=${FUZZ_CORPUS_SEED:-1} -max_total_time=${FUZZ_CORPUS_TIME:-60} -max_len=2048 -rss_limit_mb=8192 -timeout=30 || true

# Passing files instead of a directory will run fuzzing as a regression test.
# TODO(asraa): Remove manual `|| true`, but this shouldn't be necessary. 
${TEST_BINARY} $(find fuzz_corpus -type f) -rss_limit_mb=8192 || true
