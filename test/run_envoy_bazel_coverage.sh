#!/bin/bash

set -e

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"
[[ -z "${GCOVR_DIR}" ]] && GCOVR_DIR="${SRCDIR}/bazel-envoy"
[[ -z "${TESTLOGS_DIR}" ]] && TESTLOGS_DIR="${SRCDIR}/bazel-testlogs"
[[ -z "${BAZEL_COVERAGE}" ]] && BAZEL_COVERAGE=bazel
[[ -z "${GCOVR}" ]] && GCOVR=gcovr

# Make sure //test/coverage is up-to-date.
(BAZEL_BIN="${BAZEL_COVERAGE}" "${SRCDIR}"/test/coverage/gen_build.sh)

# Run all tests under bazel coverage.
"${BAZEL_COVERAGE}" coverage //test/coverage:coverage_tests ${BAZEL_BUILD_OPTIONS} \
  --cache_test_results=no --instrumentation_filter="" \
  --coverage_support=@bazel_tools//tools/coverage:coverage_support

# Cleanup any artifacts from previous coverage runs.
rm -f $(find "${GCOVR_DIR}" -name "*.gcda" -o -name "*.gcov")

# Unpack .gcda.gcno from coverage run. This needs to be done in the Envoy source directory with
# bazel-out/ underneath it since gcov expects files to be available at their relative build
# location. This can be done somewhere else, e.g. /tmp, but it involves complex copying or
# symlinking of files to that location.
(cd "${GCOVR_DIR}"; tar -xvf "${TESTLOGS_DIR}"/test/coverage/coverage_tests/coverage.dat)

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
