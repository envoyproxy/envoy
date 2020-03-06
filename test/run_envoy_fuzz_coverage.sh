#!/bin/bash

set -e

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"

echo "Starting run_envoy_fuzz_coverage.sh..."
echo "    PWD=$(pwd)"
echo "    SRCDIR=${SRCDIR}"

# This is the fuzz target that will be run to generate coverage data.
if [[ $# -gt 0 ]]; then
  FUZZ_TARGETS=$*
else
  # By default, this will be all fuzz targets.
  FUZZER_TARGETS_CC=$(find . -name *_fuzz_test.cc)
  FUZZER_TARGETS="$(for t in ${FUZZER_TARGETS_CC}; do echo "${t:2:-3}"; done)"
  FUZZ_TARGETS=""
  for t in ${FUZZER_TARGETS}
  do
    FUZZ_TARGETS+="//"$(dirname "$t")":"$(basename "$t")" "
  done
fi

echo ${FUZZ_TARGETS}

# Build all fuzz targets to run instrumented with libfuzzer in sequence.
echo "Building fuzz targets..."
for t in ${FUZZ_TARGETS}
do
  bazel build "${t}_with_libfuzzer" --config asan-fuzzer -c opt
done

# Now run each fuzz target in parallel for 60 seconds.
pids=""
TEMP_CORPORA=""
echo "Running fuzz targets..."
for t in ${FUZZ_TARGETS}
do
  # Get the original corpus for the fuzz target
  ORIGINAL_CORPUS=$(bazel query "labels(srcs, ${t}_corpus)" | head -1)
  ORIGINAL_CORPUS=${ORIGINAL_CORPUS/://}
  ORIGINAL_CORPUS=$(dirname ${ORIGINAL_CORPUS})
  # Create temp directory in target's corpus
  CORPUS_DIR=$(mktemp -d -p $(pwd)/${ORIGINAL_CORPUS:2})
  TEMP_CORPORA+="${CORPUS_DIR} "
  # Run fuzzing process.
  TARGET_BINARY="${t/://}"
  bazel-bin/${TARGET_BINARY:2}_with_libfuzzer -max_total_time=60 ${CORPUS_DIR} $(pwd)${ORIGINAL_CORPUS:1} &
  pids="$pids $!"
done

# Wait for background process to run.
# TODO? Processes will still run in background if user ctrl-c.
for pid in $pids; do
  wait ${pid}
  if [ $? -eq 0 ]; then
    echo "SUCCESS for $pid"
  else
    echo "FAILED for $pid"
  fi
done

# This will be used by llvm-cov and points to the binaries.
OBJECTS=""

# TODO can I re-use cached targets from bazel build and bazel coverage?
echo "Running bazel coverage for each target..."
for t in ${FUZZ_TARGETS}
do
  # TODO can I also grab the corpus more easily. ${t}_corpus doesn't always work for cloudesf etc
  ORIGINAL_CORPUS=$(bazel query "labels(srcs, ${t}_corpus)" | head -1)
  ORIGINAL_CORPUS=${ORIGINAL_CORPUS/://}
  ORIGINAL_CORPUS=$(dirname ${ORIGINAL_CORPUS})
  BAZEL_USE_LLVM_NATIVE_COVERAGE=1 GCOV=llvm-profdata bazel coverage ${BAZEL_BUILD_OPTIONS} \
    --instrumentation_filter=//source/...,//include/... \
    --test_timeout=2000 --cxxopt="-DENVOY_CONFIG_COVERAGE=1" --test_output=streamed \
    --test_env=HEAPCHECK= "${t}_with_libfuzzer" --test_arg=$(pwd)${ORIGINAL_CORPUS:1} --test_arg=-runs=0
  TARGET_BINARY="${t/://}"
  if [[ -z $OBJECTS ]]; then
    # The first object needs to be passed without -object= flag.
    OBJECTS="bazel-bin/${TARGET_BINARY:2}_with_libfuzzer"
  else
    OBJECTS="$OBJECTS -object=bazel-bin/${TARGET_BINARY:2}_with_libfuzzer"
  fi
done

COVERAGE_DIR="${SRCDIR}"/generated/coverage
mkdir -p "${COVERAGE_DIR}"

COVERAGE_IGNORE_REGEX="(/external/|pb\.(validate\.)?(h|cc)|/chromium_url/|/test/|/tmp|/source/extensions/quic_listeners/quiche/)"
COVERAGE_DATA="${COVERAGE_DIR}/coverage.dat"

echo "Merging coverage data..."
llvm-profdata merge -sparse -o ${COVERAGE_DATA} $(find -L bazel-out/k8-fastbuild/testlogs/test/ -name coverage.dat)

echo "Generating report..."
llvm-cov show ${OBJECTS} -instr-profile="${COVERAGE_DATA}" -Xdemangler=c++filt \
  -ignore-filename-regex="${COVERAGE_IGNORE_REGEX}" -output-dir=${COVERAGE_DIR} -format=html
sed -i -e 's|>proc/self/cwd/|>|g' "${COVERAGE_DIR}/index.html"
sed -i -e 's|>bazel-out/[^/]*/bin/\([^/]*\)/[^<]*/_virtual_includes/[^/]*|>\1|g' "${COVERAGE_DIR}/index.html"

[[ -z "${ENVOY_COVERAGE_DIR}" ]] || rsync -av "${COVERAGE_DIR}"/ "${ENVOY_COVERAGE_DIR}"

# Clean up...
for corpus in ${TEMP_CORPORA}
do
  rm -rf $corpus
done


if [ "$VALIDATE_COVERAGE" == "true" ]
then
  COVERAGE_VALUE=$(llvm-cov export "${COVERAGE_BINARY}" -instr-profile="${COVERAGE_DATA}" \
    -ignore-filename-regex="${COVERAGE_IGNORE_REGEX}" -summary-only | \
    python3 -c "import sys, json; print(json.load(sys.stdin)['data'][0]['totals']['lines']['percent'])")
  COVERAGE_THRESHOLD=97.0
  COVERAGE_FAILED=$(echo "${COVERAGE_VALUE}<${COVERAGE_THRESHOLD}" | bc)
  if test ${COVERAGE_FAILED} -eq 1; then
      echo Code coverage ${COVERAGE_VALUE} is lower than limit of ${COVERAGE_THRESHOLD}
      exit 1
  else
      echo Code coverage ${COVERAGE_VALUE} is good and higher than limit of ${COVERAGE_THRESHOLD}
  fi
fi
echo "HTML coverage report is in ${COVERAGE_DIR}/index.html"



