#!/bin/bash

if [[ $# -gt 0 ]]; then
  FUZZ_TARGETS=$*
else
  echo "This script should be called from tools/run_envoy_bazel_coverage.sh"
fi

LIBFUZZER_TARGETS=""
# Build all fuzz targets to run instrumented with libfuzzer in sequence.
for t in ${FUZZ_TARGETS}
do
  LIBFUZZER_TARGETS+="${t}_with_libfuzzer "
done

bazel build ${BAZEL_BUILD_OPTIONS} ${LIBFUZZER_TARGETS} --config asan-fuzzer -c opt

# Now run each fuzz target in parallel for 60 seconds.
PIDS=""
TMPDIR="${FUZZ_TEMPDIR}"

for t in ${FUZZ_TARGETS}
do
  # Make a temporary corpus for this fuzz target.
  TARGET_BINARY="${t/://}"
  TEMP_CORPUS_PATH="${TARGET_BINARY:2}"
  CORPUS_DIR="${TMPDIR}/${TEMP_CORPUS_PATH////_}_corpus"
  mkdir -v "${CORPUS_DIR}"
  # Get the original corpus for the fuzz target
  CORPUS_LOCATION="$(bazel query "labels(data, ${t})" | head -1)"
  ORIGINAL_CORPUS="$(bazel query "labels(srcs, ${CORPUS_LOCATION})" | head -1)"
  ORIGINAL_CORPUS="${ORIGINAL_CORPUS/://}"
  ORIGINAL_CORPUS="$(dirname ${ORIGINAL_CORPUS})"
  # Copy entries in original corpus into temp.
  cp -r "$(pwd)${ORIGINAL_CORPUS:1}" "${CORPUS_DIR}"
  # Run fuzzing process.
  bazel-bin/"${TARGET_BINARY:2}"_with_libfuzzer -max_total_time=60 "${CORPUS_DIR}" &
  # Add pid to pids list
  PIDS="${PIDS} $!"
done

# Wait for background process to run.
for pid in ${PIDS}; do
  wait $pid
  if [ $? -ne 0 ]; then
    echo "${pid} FAILED"
  fi
done
