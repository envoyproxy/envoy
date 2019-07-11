#!/bin/bash

set -e

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"
[[ -z "${GCOVR_DIR}" ]] && GCOVR_DIR="${SRCDIR}/bazel-$(basename "${SRCDIR}")"
[[ -z "${TESTLOGS_DIR}" ]] && TESTLOGS_DIR="${SRCDIR}/bazel-testlogs"
[[ -z "${BAZEL_COVERAGE}" ]] && BAZEL_COVERAGE=bazel
[[ -z "${GCOVR}" ]] && GCOVR=gcovr
[[ -z "${WORKSPACE}" ]] && WORKSPACE=envoy
[[ -z "${VALIDATE_COVERAGE}" ]] && VALIDATE_COVERAGE=true

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
# Command-line arguments take precedence over ${COVERAGE_TARGET}.
if [[ $# -gt 0 ]]; then
  COVERAGE_TARGETS=$*
elif [[ -n "${COVERAGE_TARGET}" ]]; then
  COVERAGE_TARGETS=${COVERAGE_TARGET}
else
  COVERAGE_TARGETS=//test/...
fi

# This is where we are going to copy the .gcno files into.
GCNO_ROOT=bazel-out/k8-dbg/bin/test/coverage/coverage_tests.runfiles/"${WORKSPACE}"
echo "    GCNO_ROOT=${GCNO_ROOT}"

echo "Cleaning .gcno from previous coverage runs..."
NUM_PREVIOUS_GCNO_FILES=0
for f in $(find -L "${GCNO_ROOT}" -name "*.gcno")
do
  rm -f "${f}"
  let NUM_PREVIOUS_GCNO_FILES=NUM_PREVIOUS_GCNO_FILES+1
done
echo "Cleanup completed. ${NUM_PREVIOUS_GCNO_FILES} files deleted."

# Make sure //test/coverage:coverage_tests is up-to-date.
SCRIPT_DIR="$(realpath "$(dirname "$0")")"
(BAZEL_BIN="${BAZEL_COVERAGE}" "${SCRIPT_DIR}"/coverage/gen_build.sh ${COVERAGE_TARGETS})

echo "Cleaning .gcda/.gcov from previous coverage runs..."
NUM_PREVIOUS_GCOV_FILES=0
for f in $(find -L "${GCOVR_DIR}" -name "*.gcda" -o -name "*.gcov")
do
  rm -f "${f}"
  let NUM_PREVIOUS_GCOV_FILES=NUM_PREVIOUS_GCOV_FILES+1
done
echo "Cleanup completed. ${NUM_PREVIOUS_GCOV_FILES} files deleted."

# Force dbg for path consistency later, don't include debug code in coverage.
BAZEL_BUILD_OPTIONS="${BAZEL_BUILD_OPTIONS} -c dbg --copt=-DNDEBUG"

# Run all tests under "bazel test", no sandbox. We're going to generate the
# .gcda inplace in the bazel-out/ directory. This is in contrast to the "bazel
# coverage" method, which is currently broken for C++ (see
# https://github.com/bazelbuild/bazel/issues/1118). This works today as we have
# a single coverage test binary and do not require the "bazel coverage" support
# for collecting multiple traces and glueing them together.
"${BAZEL_COVERAGE}" test //test/coverage:coverage_tests ${BAZEL_BUILD_OPTIONS} \
  --cache_test_results=no --cxxopt="--coverage" --cxxopt="-DENVOY_CONFIG_COVERAGE=1" \
  --linkopt="--coverage" --define ENVOY_CONFIG_COVERAGE=1 --test_output=streamed \
  --strategy=Genrule=standalone --spawn_strategy=standalone --test_timeout=4000 \
  --test_arg="--log-path /dev/null" --test_arg="-l trace"

# The Bazel build has a lot of whack in it, in particular generated files, headers from external
# deps, etc. So, we exclude this from gcov to avoid false reporting of these files in the html and
# stats. The #foo# pattern is because gcov produces files such as
# bazel-out#local-fastbuild#bin#external#spdlog_git#_virtual_includes#spdlog#spdlog#details#pattern_formatter_impl.h.gcov.
# To find these while modifying this regex, perform a gcov run with -k set.
[[ -z "${GCOVR_EXCLUDE_REGEX}" ]] && GCOVR_EXCLUDE_REGEX=".*pb.h.gcov|.*#k8-dbg#bin#.*|test#.*|external#.*|.*#external#.*|.*#prebuilt#.*|.*#config_validation#.*|.*#chromium_url#.*"
[[ -z "${GCOVR_EXCLUDE_DIR}" ]] && GCOVR_EXCLUDE_DIR=".*/external/.*"

COVERAGE_DIR="${SRCDIR}"/generated/coverage
mkdir -p "${COVERAGE_DIR}"
COVERAGE_SUMMARY="${COVERAGE_DIR}/coverage_summary.txt"

# Copy .gcno objects into the same location that we find the .gcda.
# TODO(htuch): Should use rsync, but there are some symlink loops to fight.
echo "Finding and copying .gcno files in GCOVR_DIR: ${GCOVR_DIR}"
mkdir -p ${GCNO_ROOT}
NUM_GCNO_FILES=0
for f in $(find -L bazel-out/ -name "*.gcno")
do
  cp --parents "$f" ${GCNO_ROOT}/
  let NUM_GCNO_FILES=NUM_GCNO_FILES+1
done
echo "OK: copied ${NUM_GCNO_FILES} .gcno files"

# gcovr is extremely picky about where it is run and where the paths of the
# original source are relative to its execution location.
cd -P "${GCOVR_DIR}"
echo "Running gcovr in $(pwd)..."
time "${GCOVR}" -v --gcov-exclude="${GCOVR_EXCLUDE_REGEX}" \
  --exclude-directories="${GCOVR_EXCLUDE_DIR}" -r . \
  --html --html-details --exclude-unreachable-branches --print-summary \
  -o "${COVERAGE_DIR}"/coverage.html > "${COVERAGE_SUMMARY}"

# Clean up the generated test/coverage/BUILD file: subsequent bazel invocations
# can choke on it if it references things that changed since the last coverage
# run.
rm "${SRCDIR}"/test/coverage/BUILD

[[ -z "${ENVOY_COVERAGE_DIR}" ]] || rsync -av "${COVERAGE_DIR}"/ "${ENVOY_COVERAGE_DIR}"

if [ "$VALIDATE_COVERAGE" == "true" ]
then
  COVERAGE_VALUE=$(grep -Po 'lines: \K(\d|\.)*' "${COVERAGE_SUMMARY}")
  COVERAGE_THRESHOLD=97.5
  COVERAGE_FAILED=$(echo "${COVERAGE_VALUE}<${COVERAGE_THRESHOLD}" | bc)
  if test ${COVERAGE_FAILED} -eq 1; then
      echo Code coverage ${COVERAGE_VALUE} is lower than limit of ${COVERAGE_THRESHOLD}
      exit 1
  else
      echo Code coverage ${COVERAGE_VALUE} is good and higher than limit of ${COVERAGE_THRESHOLD}
  fi
  echo "HTML coverage report is in ${COVERAGE_DIR}/coverage.html"
fi
