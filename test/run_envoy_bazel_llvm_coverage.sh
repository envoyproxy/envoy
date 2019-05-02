#!/bin/bash

set -e

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"
[[ -z "${BAZEL_COVERAGE}" ]] && BAZEL_COVERAGE=bazel
[[ -z "${WORKSPACE}" ]] && WORKSPACE=envoy

echo "Starting run_envoy_bazel_coverage.sh..."
echo "    PWD=$(pwd)"
echo "    SRCDIR=${SRCDIR}"
echo "    GCOVR_DIR=${GCOVR_DIR}"
echo "    TESTLOGS_DIR=${TESTLOGS_DIR}"
echo "    BAZEL_COVERAGE=${BAZEL_COVERAGE}"
echo "    GCOVR=${GCOVR}"
echo "    WORKSPACE=${WORKSPACE}"
echo "    VALIDATE_COVERAGE=${VALIDATE_COVERAGE}"

# This is the target that will be run to generate coverage data. It can be overridden by consumer
# projects that want to run coverage on a different/combined target.
[[ -z "${COVERAGE_TARGET}" ]] && COVERAGE_TARGET="//test/..."

"${BAZEL_COVERAGE}" test "${COVERAGE_TARGET}" ${BAZEL_TEST_OPTIONS} \
  --cache_test_results=no --define ENVOY_CONFIG_COVERAGE=llvm --test_output=all \
  --strategy=Genrule=standalone --strategy=TestRunner=standalone \
  --test_filter='-QuicPlatformTest.QuicStackTraceTest:IpVersions/ClusterMemoryTestRunner.*' \
  --test_env=LLVM_PROFILE_FILE=test.profraw

COVERAGE_DIR="${SRCDIR}"/generated/coverage
mkdir -p "${COVERAGE_DIR}"

echo "Merging profile data..."
llvm-profdata merge -sparse $(find -L bazel-bin -name test.profraw) -o ${COVERAGE_DIR}/coverage.profdata

echo "Generating report..."
# TODO(lizan): fix path, exclude external headers
llvm-cov show bazel-bin/source/exe/envoy-static -instr-profile=${COVERAGE_DIR}/coverage.profdata -output-dir=${COVERAGE_DIR} --format=html

[[ -z "${ENVOY_COVERAGE_DIR}" ]] || rsync -av "${COVERAGE_DIR}"/ "${ENVOY_COVERAGE_DIR}"
