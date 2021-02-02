#!/bin/bash

set -e

LLVM_VERSION="10.0.0"
CLANG_VERSION=$(clang --version | grep version | sed -e 's/\ *clang version \(.*\)\ /\1/')
LLVM_COV_VERSION=$(llvm-cov --version | grep version | sed -e 's/\ *LLVM version \(.*\)/\1/')
LLVM_PROFDATA_VERSION=$(llvm-profdata show --version | grep version | sed -e 's/\ *LLVM version \(.*\)/\1/')

if [ "${CLANG_VERSION}" != "${LLVM_VERSION}" ]
then
  echo "clang version ${CLANG_VERSION} does not match expected ${LLVM_VERSION}"
  exit 1
fi

if [ "${LLVM_COV_VERSION}" != "${LLVM_VERSION}" ]
then
  echo "llvm-cov version ${LLVM_COV_VERSION} does not match expected ${LLVM_VERSION}"
  exit 1
fi

if [ "${LLVM_PROFDATA_VERSION}" != "${LLVM_VERSION}" ]
then
  echo "llvm-profdata version ${LLVM_PROFDATA_VERSION} does not match expected ${LLVM_VERSION}"
  exit 1
fi

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"
[[ -z "${VALIDATE_COVERAGE}" ]] && VALIDATE_COVERAGE=true
[[ -z "${FUZZ_COVERAGE}" ]] && FUZZ_COVERAGE=false
[[ -z "${COVERAGE_THRESHOLD}" ]] && COVERAGE_THRESHOLD=96.5
COVERAGE_TARGET="${COVERAGE_TARGET:-}"
read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTIONS:-}"

echo "Starting run_envoy_bazel_coverage.sh..."
echo "    PWD=$(pwd)"
echo "    SRCDIR=${SRCDIR}"
echo "    VALIDATE_COVERAGE=${VALIDATE_COVERAGE}"

# This is the target that will be run to generate coverage data. It can be overridden by consumer
# projects that want to run coverage on a different/combined target.
# Command-line arguments take precedence over ${COVERAGE_TARGET}.
if [[ $# -gt 0 ]]; then
  COVERAGE_TARGETS=("$@")
elif [[ -n "${COVERAGE_TARGET}" ]]; then
  COVERAGE_TARGETS=("${COVERAGE_TARGET}")
else
  COVERAGE_TARGETS=(//test/...)
fi

if [[ "${FUZZ_COVERAGE}" == "true" ]]; then
  # Filter targets to just fuzz tests.
  _targets=$(bazel query "attr('tags', 'fuzz_target', ${COVERAGE_TARGETS[*]})")
  COVERAGE_TARGETS=()
  while read -r line; do COVERAGE_TARGETS+=("$line"); done \
      <<< "$_targets"
  BAZEL_BUILD_OPTIONS+=(
      "--config=fuzz-coverage"
      "--test_tag_filters=-nocoverage")
else
  BAZEL_BUILD_OPTIONS+=(
      "--config=test-coverage"
      "--test_tag_filters=-nocoverage,-fuzz_target")
fi

bazel coverage "${BAZEL_BUILD_OPTIONS[@]}" "${COVERAGE_TARGETS[@]}"

# Collecting profile and testlogs
[[ -z "${ENVOY_BUILD_PROFILE}" ]] || cp -f "$(bazel info output_base)/command.profile.gz" "${ENVOY_BUILD_PROFILE}/coverage.profile.gz" || true
[[ -z "${ENVOY_BUILD_DIR}" ]] || find bazel-testlogs/ -name test.log | tar zcf "${ENVOY_BUILD_DIR}/testlogs.tar.gz" -T -

COVERAGE_DIR="${SRCDIR}"/generated/coverage && [[ ${FUZZ_COVERAGE} == "true" ]] && COVERAGE_DIR="${SRCDIR}"/generated/fuzz_coverage

rm -rf "${COVERAGE_DIR}"
mkdir -p "${COVERAGE_DIR}"

COVERAGE_DATA="${COVERAGE_DIR}/coverage.dat"
cp bazel-out/_coverage/_coverage_report.dat "${COVERAGE_DATA}"

COVERAGE_VALUE="$(genhtml --prefix "${PWD}" --output "${COVERAGE_DIR}" "${COVERAGE_DATA}" | tee /dev/stderr | grep lines... | cut -d ' ' -f 4)"
COVERAGE_VALUE=${COVERAGE_VALUE%?}

if [ "${FUZZ_COVERAGE}" == "true" ]
then
  [[ -z "${ENVOY_FUZZ_COVERAGE_ARTIFACT}" ]] || tar zcf "${ENVOY_FUZZ_COVERAGE_ARTIFACT}" -C "${COVERAGE_DIR}" --transform 's/^\./fuzz_coverage/' .
else
  [[ -z "${ENVOY_COVERAGE_ARTIFACT}" ]] || tar zcf "${ENVOY_COVERAGE_ARTIFACT}" -C "${COVERAGE_DIR}" --transform 's/^\./coverage/' .
fi

if [[ "$VALIDATE_COVERAGE" == "true" ]]; then
  if [[ "${FUZZ_COVERAGE}" == "true" ]]; then
    COVERAGE_THRESHOLD=27.0
  fi
  COVERAGE_FAILED=$(echo "${COVERAGE_VALUE}<${COVERAGE_THRESHOLD}" | bc)
  if [[ "${COVERAGE_FAILED}" -eq 1 ]]; then
      echo "##vso[task.setvariable variable=COVERAGE_FAILED]${COVERAGE_FAILED}"
      echo "Code coverage ${COVERAGE_VALUE} is lower than limit of ${COVERAGE_THRESHOLD}"
      exit 1
  else
      echo "Code coverage ${COVERAGE_VALUE} is good and higher than limit of ${COVERAGE_THRESHOLD}"
  fi
fi

# We want to allow per_file_coverage to fail without exiting this script.
set +e
if [[ "$VALIDATE_COVERAGE" == "true" ]] && [[ "${FUZZ_COVERAGE}" == "false" ]]; then
  echo "Checking per-extension coverage"
  output=$(./test/per_file_coverage.sh)

  if [ $? -eq 1 ]; then
    echo Per-extension coverage failed:
    echo "$output"
    COVERAGE_FAILED=1
    echo "##vso[task.setvariable variable=COVERAGE_FAILED]${COVERAGE_FAILED}"
    exit 1
  fi
  echo Per-extension coverage passed.
fi

echo "HTML coverage report is in ${COVERAGE_DIR}/index.html"
