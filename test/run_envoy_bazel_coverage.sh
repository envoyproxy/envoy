#!/bin/bash

set -e

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"
[[ -z "${REAL_SRCDIR}" ]] && REAL_SRCDIR="${SRCDIR}"
[[ -z "${GCOVR_DIR}" ]] && GCOVR_DIR="${SRCDIR}/bazel-$(basename "${SRCDIR}")"
[[ -z "${TESTLOGS_DIR}" ]] && TESTLOGS_DIR="${SRCDIR}/bazel-testlogs"
[[ -z "${BAZEL_COVERAGE}" ]] && BAZEL_COVERAGE=bazel
[[ -z "${GCOVR}" ]] && GCOVR=gcovr

# Make sure //test/coverage is up-to-date.
(BAZEL_BIN="${BAZEL_COVERAGE}" "${REAL_SRCDIR}"/test/coverage/gen_build.sh)

echo "Cleaning .gcda/.gcov from previous coverage runs..."
for f in $(find -L "${GCOVR_DIR}" -name "*.gcda" -o -name "*.gcov")
do
  rm -f "${f}"
done
echo "Cleanup completed."

# Run all tests under "bazel test", no sandbox. We're going to generate the
# .gcda inplace in the bazel-out/ directory. This is in contrast to the "bazel
# coverage" method, which is currently broken for C++ (see
# https://github.com/bazelbuild/bazel/issues/1118). This works today as we have
# a single coverage test binary and do not require the "bazel coverage" support
# for collecting multiple traces and glueing them together.
"${BAZEL_COVERAGE}" test //test/coverage:coverage_tests ${BAZEL_BUILD_OPTIONS} \
  --cache_test_results=no --cxxopt="--coverage" --linkopt="--coverage" \
  --test_output=all --strategy=Genrule=standalone --spawn_strategy=standalone

# The Bazel build has a lot of whack in it, in particular generated files, headers from external
# deps, etc. So, we exclude this from gcov to avoid false reporting of these files in the html and
# stats. The #foo# pattern is because gcov produces files such as
# bazel-out#local-fastbuild#bin#external#spdlog_git#_virtual_includes#spdlog#spdlog#details#pattern_formatter_impl.h.gcov.
# To find these while modifying this regex, perform a gcov run with -k set.
GCOVR_EXCLUDE_REGEX=".*pb.h.gcov|.*#genfiles#.*|test#.*|external#.*|.*#external#.*|.*#prebuilt#.*"

COVERAGE_DIR="${SRCDIR}"/generated/coverage
mkdir -p "${COVERAGE_DIR}"
COVERAGE_SUMMAY="${COVERAGE_DIR}/coverage_summary.txt"

# gcovr is extremely picky about where it is run and where the paths of the
# original source are relative to its execution location.
cd "${SRCDIR}"
echo "Running gcovr..."
time "${GCOVR}" --gcov-exclude="${GCOVR_EXCLUDE_REGEX}" \
  --exclude-directories=".*/external/.*" --object-directory="${GCOVR_DIR}" -r "${SRCDIR}" \
  --html --html-details --exclude-unreachable-branches --print-summary \
  -o "${COVERAGE_DIR}"/coverage.html > "${COVERAGE_SUMMAY}"

COVERAGE_VALUE=$(grep -Po 'lines: \K(\d|\.)*' "${COVERAGE_SUMMAY}")
COVERAGE_THRESHOLD=98.0
COVERAGE_FAILED=$(echo "${COVERAGE_VALUE}<${COVERAGE_THRESHOLD}" | bc)
if test ${COVERAGE_FAILED} -eq 1; then
    echo Code coverage ${COVERAGE_VALUE} is lower than limit of ${COVERAGE_THRESHOLD}
    exit 1
else
    echo Code coverage ${COVERAGE_VALUE} is good and higher than limit of ${COVERAGE_THRESHOLD}
fi
