#!/bin/bash

set -e

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"
[[ -z "${VALIDATE_COVERAGE}" ]] && VALIDATE_COVERAGE=true

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

# Make sure //test/coverage:coverage_tests is up-to-date.
SCRIPT_DIR="$(realpath "$(dirname "$0")")"
"${SCRIPT_DIR}"/coverage/gen_build.sh ${COVERAGE_TARGETS}

# Using GTEST_SHUFFLE here to workaround https://github.com/envoyproxy/envoy/issues/10108
BAZEL_USE_LLVM_NATIVE_COVERAGE=1 GCOV=llvm-profdata bazel coverage ${BAZEL_BUILD_OPTIONS} \
    -c fastbuild --copt=-DNDEBUG --instrumentation_filter=//source/...,//include/... \
    --test_timeout=2000 --cxxopt="-DENVOY_CONFIG_COVERAGE=1" --test_output=errors \
    --test_arg="--log-path /dev/null" --test_arg="-l trace" --test_env=HEAPCHECK= \
    --test_env=GTEST_SHUFFLE=1 --flaky_test_attempts=5 //test/coverage:coverage_tests

COVERAGE_DIR="${SRCDIR}"/generated/coverage
mkdir -p "${COVERAGE_DIR}"

COVERAGE_IGNORE_REGEX="(/external/|pb\.(validate\.)?(h|cc)|/chromium_url/|/test/|/tmp|/source/extensions/quic_listeners/quiche/)"
COVERAGE_BINARY="bazel-bin/test/coverage/coverage_tests"
COVERAGE_DATA="${COVERAGE_DIR}/coverage.dat"

echo "Merging coverage data..."
llvm-profdata merge -sparse -o ${COVERAGE_DATA} $(find -L bazel-out/k8-fastbuild/testlogs/test/coverage/coverage_tests/ -name coverage.dat)

echo "Generating report..."
llvm-cov show "${COVERAGE_BINARY}" -instr-profile="${COVERAGE_DATA}" -Xdemangler=c++filt \
  -ignore-filename-regex="${COVERAGE_IGNORE_REGEX}" -output-dir=${COVERAGE_DIR} -format=html
sed -i -e 's|>proc/self/cwd/|>|g' "${COVERAGE_DIR}/index.html"
sed -i -e 's|>bazel-out/[^/]*/bin/\([^/]*\)/[^<]*/_virtual_includes/[^/]*|>\1|g' "${COVERAGE_DIR}/index.html"

[[ -z "${ENVOY_COVERAGE_DIR}" ]] || rsync -av "${COVERAGE_DIR}"/ "${ENVOY_COVERAGE_DIR}"

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
