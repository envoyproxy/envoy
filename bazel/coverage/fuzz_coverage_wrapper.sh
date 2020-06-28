#!/bin/bash

set -ex

TEST_BINARY=$1
shift

# Clear existing corpus if previous run wasn't in sandbox
rm -rf fuzz_corpus

mkdir -p fuzz_corpus/seed_corpus
cp -r $@ fuzz_corpus/seed_corpus

${TEST_BINARY} fuzz_corpus -seed=${FUZZ_CORPUS_SEED:-1} -max_total_time=${FUZZ_CORPUS_TIME:-60}
