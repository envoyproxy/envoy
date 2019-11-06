#!/bin/bash

set -e

# run fuzzing regression or upload to Fuzzit for long running fuzzing job depending on whether FUZZIT_API_KEY is set

FUZZIT="${TEST_SRCDIR}/fuzzit_linux/fuzzit"

FUZZER_BINARY=$1
FUZZIT_TARGET_NAME="$(basename $1 | sed -e s/_fuzz_test_with_libfuzzer$// -e s/_/-/g)"

if [[ ! -z "${FUZZIT_API_KEY}" ]]; then
  "${FUZZIT}" create target --skip-if-exists --public-corpus envoyproxy/"${FUZZIT_TARGET_NAME}"

  # Run fuzzing first so this is not affected by local-regression timeout
  "${FUZZIT}" create job --skip-if-not-exists --type fuzzing envoyproxy/"${FUZZIT_TARGET_NAME}" "${FUZZER_BINARY}"
fi

"${FUZZIT}" create job --skip-if-not-exists --type local-regression envoyproxy/"${FUZZIT_TARGET_NAME}" "${FUZZER_BINARY}"
