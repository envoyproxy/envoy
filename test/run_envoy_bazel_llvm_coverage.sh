#!/bin/bash

set -e

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"

echo "Starting run_envoy_bazel_coverage.sh..."
echo "    PWD=$(pwd)"
echo "    SRCDIR=${SRCDIR}"
echo "    VALIDATE_COVERAGE=${VALIDATE_COVERAGE}"

# This is the target that will be run to generate coverage data. It can be overridden by consumer
# projects that want to run coverage on a different/combined target.
# Command-line arguments take precedence over ${COVERAGE_TARGET}.
if [[ $# -gt 0 ]]; then
  COVERAGE_TARGETS=$*
elif [[ -n "${COVERAGE_TARGET}" ]]; then
  COVERAGE_TARGETS=${COVERAGE_TARGET}
else
  COVERAGE_TARGETS=//test/...
fi

rm -rf $(find -L bazel-bin -name "test-*.profraw")

# Make sure //test/coverage:coverage_tests is up-to-date.
SCRIPT_DIR="$(realpath "$(dirname "$0")")"
NO_GCOV=1 "${SCRIPT_DIR}"/coverage/gen_build.sh ${COVERAGE_TARGETS}

BAZEL_USE_LLVM_NATIVE_COVERAGE=1 GCOV=llvm-profdata bazel coverage ${BAZEL_BUILD_OPTIONS} \
    -c fastbuild --copt=-DNDEBUG --instrumentation_filter=//source/...,//include/... --dynamic_mode=off --test_timeout=4000 \
    --strategy=TestRunner=local --test_sharding_strategy=disabled --test_arg="--log-path /dev/null" --test_arg="-l trace" \
    --test_env=HEAPCHECK= --test_filter='-QuicPlatformTest.QuicStackTraceTest' //test/coverage:coverage_tests

COVERAGE_DIR="${SRCDIR}"/generated/coverage
mkdir -p "${COVERAGE_DIR}"

echo "Merging profile data..."
llvm-profdata merge -sparse $(find -L bazel-out -name coverage.dat) -o ${COVERAGE_DIR}/coverage.profdata

echo "Generating report..."
llvm-cov show bazel-bin/test/coverage/coverage_tests -instr-profile=${COVERAGE_DIR}/coverage.profdata \
  -ignore-filename-regex='(/external/|pb\.(validate\.)?(h|cc)|/chromium_url/|/test/|/tmp)' -output-dir=${COVERAGE_DIR} -format=html
sed -i -e 's|>proc/self/cwd/|>|g' "${COVERAGE_DIR}/index.html"
sed -i -e 's|>bazel-out/[^/]*/bin/\([^/]*\)/[^<]*/_virtual_includes/[^/]*|>\1|g' "${COVERAGE_DIR}/index.html"

[[ -z "${ENVOY_COVERAGE_DIR}" ]] || rsync -av "${COVERAGE_DIR}"/ "${ENVOY_COVERAGE_DIR}"
