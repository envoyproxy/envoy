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

BAZEL_USE_LLVM_NATIVE_COVERAGE=1 GCOV=llvm-profdata bazel coverage ${BAZEL_BUILD_OPTIONS} \
    -c fastbuild --copt=-DNDEBUG --instrumentation_filter=//source/...,//include/... --dynamic_mode=off \
    --strategy=TestRunner=local --test_sharding_strategy=disabled \
    --test_env=HEAPCHECK= --test_filter='-QuicPlatformTest.QuicStackTraceTest' ${COVERAGE_TARGETS}

COVERAGE_DIR="${SRCDIR}"/generated/coverage
mkdir -p "${COVERAGE_DIR}"

echo "Merging profile data..."
llvm-profdata merge -sparse $(find -L bazel-out -name coverage.dat) -o ${COVERAGE_DIR}/coverage.profdata

echo "Generating report..."
llvm-cov show bazel-bin/source/exe/envoy-static -instr-profile=${COVERAGE_DIR}/coverage.profdata \
  -ignore-filename-regex='(/external/|pb\.(validate\.)?(h|cc)|/chromium_url/|/tmp)' -output-dir=${COVERAGE_DIR} -format=html
sed -i -e 's|>proc/self/cwd/|>|g' "${COVERAGE_DIR}/index.html"
sed -i -e 's|>bazel-out/[^/]*/bin/\([^/]*\)/[^<]*/_virtual_includes/[^/]*|>\1|g' "${COVERAGE_DIR}/index.html"

[[ -z "${ENVOY_COVERAGE_DIR}" ]] || rsync -av "${COVERAGE_DIR}"/ "${ENVOY_COVERAGE_DIR}"
