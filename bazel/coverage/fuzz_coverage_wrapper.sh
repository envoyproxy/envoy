#!/bin/bash

set -ex

TEST_BINARY=$1
shift

mkdir -p fuzz_corpus/seed_corpus
cp -r $@ fuzz_corpus/seed_corpus

# Don't collect coverage when generating corpus
LLVM_PROFILE_FILE= ${TEST_BINARY} fuzz_corpus -seed=${FUZZ_CORPUS_SEED:-1} -max_total_time=${FUZZ_CORPUS_TIME:-60}

${TEST_BINARY} fuzz_corpus -runs=0
