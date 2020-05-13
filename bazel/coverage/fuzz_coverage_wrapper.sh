#!/bin/bash

set -ex

TEST_BINARY=$1
shift

mkdir -p fuzz_corpus/seed_corpus
cp -r $@ fuzz_corpus/seed_corpus

LLVM_PROFILE_FILE= ${TEST_BINARY} fuzz_corpus -max_total_time=60

${TEST_BINARY} fuzz_corpus -runs=0
