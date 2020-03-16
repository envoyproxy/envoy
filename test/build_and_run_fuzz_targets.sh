#!/bin/bash

set -e

shopt -s expand_aliases
alias bash='/usr/local/bin/bash'

if [[ $# -gt 0 ]]; then
  FUZZ_TARGETS=$*
else
  echo "This script should be called from tools/run_envoy_bazel_coverage.sh"
fi

LIBFUZZER_TARGETS=""
# Build all fuzz targets to run instrumented with libfuzzer in sequence.
for t in ${FUZZ_TARGETS}
do
  LIBFUZZER_TARGETS+=${t}_with_libfuzzer
done

bazel build ${LIBFUZZER_TARGETS} --config asan-fuzzer -c opt

# Now run each fuzz target in parallel for 60 seconds.
pids=""
TEMP_CORPORA=""

for t in ${FUZZ_TARGETS}
do
  # Get the original corpus for the fuzz target
  CORPUS_LOCATION="$(bazel query "labels(data, ${t})" | head -1)"
  ORIGINAL_CORPUS="$(bazel query "labels(srcs, ${CORPUS_LOCATION})" | head -1)"
  ORIGINAL_CORPUS=${ORIGINAL_CORPUS/://}
  ORIGINAL_CORPUS="$(dirname ${ORIGINAL_CORPUS})"
  # Create temp directory in target's corpus
  CORPUS_DIR="$(mktemp -d -p $(pwd)/${ORIGINAL_CORPUS:2})"
  TEMP_CORPORA+="${CORPUS_DIR} "
  # Run fuzzing process.
  TARGET_BINARY="${t/://}"
  bazel-bin/${TARGET_BINARY:2}_with_libfuzzer -max_total_time=60 ${CORPUS_DIR} $(pwd)${ORIGINAL_CORPUS:1} &
  # Add pid to pids list
  pids="$pids $!"
done

# Wait for background process to run.
for pid in $pids; do
  wait ${pid}
  if [ $? -ne 0 ]; then
    echo "${pid} FAILED"
  fi
done

echo ${TEMP_CORPORA}
