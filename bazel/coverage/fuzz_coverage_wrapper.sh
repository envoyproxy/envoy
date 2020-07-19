#!/bin/bash

set -x

TEST_BINARY=$1
shift

# Clear existing corpus if previous run wasn't in sandbox
rm -rf fuzz_corpus

mkdir -p fuzz_corpus/seed_corpus
cp -r $@ fuzz_corpus/seed_corpus

# TODO(asraa): When fuzz targets are stable, remove error suppression and run coverage while fuzzing.
LLVM_PROFILE_FILE= ${TEST_BINARY} fuzz_corpus -seed=${FUZZ_CORPUS_SEED:-1} -max_total_time=${FUZZ_CORPUS_TIME:-60} -max_len=2048 || true

${TEST_BINARY} fuzz_corpus -runs=0
