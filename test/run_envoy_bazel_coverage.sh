#!/bin/bash

set -e

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"
[[ -z "${VALIDATE_COVERAGE}" ]] && VALIDATE_COVERAGE=true
[[ -z "${FUZZ_COVERAGE}" ]] && FUZZ_COVERAGE=false

echo "Starting run_envoy_bazel_coverage.sh..."
echo "    PWD=$(pwd)"
echo "    SRCDIR=${SRCDIR}"
echo "    VALIDATE_COVERAGE=${VALIDATE_COVERAGE}"
echo "    FUZZ_COVERAGE=${FUZZ_COVERAGE}"

# This is the target that will be run to generate coverage data. It can be overridden by consumer
# projects that want to run coverage on a different/combined target.
# Command-line arguments take precedence over ${COVERAGE_TARGET}.
if [[ $# -gt 0 ]]; then
  COVERAGE_TARGETS=$*
elif [[ -n "${COVERAGE_TARGET}" ]]; then
  COVERAGE_TARGETS=${COVERAGE_TARGET}
else
  # For fuzz builds, this overrides to just fuzz targets.
  COVERAGE_TARGETS=//test/... && [[ ${FUZZ_COVERAGE} == "true" ]] &&
    COVERAGE_TARGETS="$(bazel query 'attr("tags", "fuzz_target", //test/...)')"
fi

SCRIPT_DIR="$(realpath "$(dirname "$0")")"
TEMP_CORPORA=""
if [ "$FUZZ_COVERAGE" == "true" ]
then
  # Build and run libfuzzer linked target, grab collect temp directories.
  FUZZ_TEMPDIR="$(mktemp -d)"
  FUZZ_TEMPDIR=${FUZZ_TEMPDIR} "${SCRIPT_DIR}"/build_and_run_fuzz_targets.sh ${COVERAGE_TARGETS}
else
  # Make sure //test/coverage:coverage_tests is up-to-date.
  "${SCRIPT_DIR}"/coverage/gen_build.sh ${COVERAGE_TARGETS}
fi

# Set the bazel targets to run.
BAZEL_TARGET=//test/coverage:coverage_tests && [[ ${FUZZ_COVERAGE} == "true" ]] && BAZEL_TARGET=${COVERAGE_TARGETS}

# Add binaries to OBJECTS to pass in to llvm-cov
OBJECTS=""
# For nornaml builds, BAZEL_TARGET only contains //test/coverage:coverage_tests
for t in ${BAZEL_TARGET}
do
  # Set test args. If normal coverage run, this is --log-path /dev/null
  if [ "$FUZZ_COVERAGE" == "true" ]
  then
    # If this is a fuzz target, set args to be the temp corpus.
    TARGET_BINARY="${t/://}"
    CORPUS_LOCATION="${TARGET_BINARY:2}"
    TEST_ARGS=(--test_arg="${FUZZ_TEMPDIR}/${CORPUS_LOCATION////_}_corpus" --test_arg="-runs=0")
    if [[ -z "${OBJECTS}" ]]; then
      # The first object needs to be passed without -object= flag.
      OBJECTS="bazel-bin/${TARGET_BINARY:2}_with_libfuzzer"
    else
      OBJECTS="$OBJECTS -object=bazel-bin/${TARGET_BINARY:2}_with_libfuzzer"
    fi
    TARGET="${t}_with_libfuzzer"
  else
    TEST_ARGS=(--test_arg="--log-path /dev/null" --test_arg="-l trace")
    OBJECTS="bazel-bin/test/coverage/coverage_tests"
    TARGET="${t}"
  fi

  BAZEL_USE_LLVM_NATIVE_COVERAGE=1 GCOV=llvm-profdata bazel coverage ${BAZEL_BUILD_OPTIONS} \
    -c fastbuild --copt=-DNDEBUG --instrumentation_filter=//source/...,//include/... \
    --test_timeout=2000 --cxxopt="-DENVOY_CONFIG_COVERAGE=1" --test_output=errors \
    "${TEST_ARGS[@]}" --test_env=HEAPCHECK=  ${TARGET}
done

COVERAGE_DIR="${SRCDIR}"/generated/coverage
mkdir -p "${COVERAGE_DIR}"

COVERAGE_IGNORE_REGEX="(/external/|pb\.(validate\.)?(h|cc)|/chromium_url/|/test/|/tmp|/tools/|/third_party/|/source/extensions/quic_listeners/quiche/)"
COVERAGE_BINARY="bazel-bin/test/coverage/coverage_tests"
COVERAGE_DATA="${COVERAGE_DIR}/coverage.dat"

echo "Merging coverage data..."
BAZEL_OUT=test/coverage/coverage_tests/ && [[ ${FUZZ_COVERAGE} ]] && BAZEL_OUT=test/
llvm-profdata merge -sparse -o ${COVERAGE_DATA} $(find -L bazel-out/k8-fastbuild/testlogs/${BAZEL_OUT} -name coverage.dat)

echo "Generating report..."
llvm-cov show -instr-profile="${COVERAGE_DATA}" ${OBJECTS} -Xdemangler=c++filt \
  -ignore-filename-regex="${COVERAGE_IGNORE_REGEX}" -output-dir=${COVERAGE_DIR} -format=html
sed -i -e 's|>proc/self/cwd/|>|g' "${COVERAGE_DIR}/index.html"
sed -i -e 's|>bazel-out/[^/]*/bin/\([^/]*\)/[^<]*/_virtual_includes/[^/]*|>\1|g' "${COVERAGE_DIR}/index.html"

[[ -z "${ENVOY_COVERAGE_DIR}" ]] || rsync -av "${COVERAGE_DIR}"/ "${ENVOY_COVERAGE_DIR}"

if [ "$VALIDATE_COVERAGE" == "true" ]
then
  COVERAGE_VALUE=$(llvm-cov export "${OBJECTS}" -instr-profile="${COVERAGE_DATA}" \
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
