#!/bin/bash

set -e

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"
[[ -z "${VALIDATE_COVERAGE}" ]] && VALIDATE_COVERAGE=true
[[ -z "${FUZZ_COVERAGE}" ]] && FUZZ_COVERAGE=false

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
  # For fuzz builds, this overrides to just fuzz targets.
  COVERAGE_TARGETS=//test/... && [[ ${FUZZ_COVERAGE} == "true" ]] &&
    COVERAGE_TARGETS="$(bazel query 'attr("tags", "fuzz_target", //test/...)')"
fi

if [[ "${FUZZ_COVERAGE}" == "true" ]]; then
  BAZEL_BUILD_OPTIONS+=" --config=fuzz-coverage --test_tag_filters=-nocoverage"
else
  BAZEL_BUILD_OPTIONS+=" --config=test-coverage --test_tag_filters=-nocoverage,-fuzz_target"
fi

bazel coverage ${BAZEL_BUILD_OPTIONS} ${COVERAGE_TARGETS}

# Collecting profile and testlogs
[[ -z "${ENVOY_BUILD_PROFILE}" ]] || cp -f "$(bazel info output_base)/command.profile.gz" "${ENVOY_BUILD_PROFILE}/coverage.profile.gz" || true
[[ -z "${ENVOY_BUILD_DIR}" ]] || find bazel-testlogs/ -name test.log | tar zcf "${ENVOY_BUILD_DIR}/testlogs.tar.gz" -T -

COVERAGE_DIR="${SRCDIR}"/generated/coverage

rm -rf "${COVERAGE_DIR}"
mkdir -p "${COVERAGE_DIR}"

COVERAGE_DATA="${COVERAGE_DIR}/coverage.dat"
cp bazel-out/_coverage/_coverage_report.dat "${COVERAGE_DATA}"

COVERAGE_VALUE=$(genhtml --prefix ${PWD} --output "${COVERAGE_DIR}" "${COVERAGE_DATA}" | tee /dev/stderr | grep lines... | cut -d ' ' -f 4)
COVERAGE_VALUE=${COVERAGE_VALUE%?}

[[ -z "${ENVOY_COVERAGE_ARTIFACT}" ]] || tar zcf "${ENVOY_COVERAGE_ARTIFACT}" -C ${COVERAGE_DIR} --transform 's/^\./coverage/' .

if [[ "$VALIDATE_COVERAGE" == "true" ]]; then
  if [[ "${FUZZ_COVERAGE}" == "true" ]]; then
    COVERAGE_THRESHOLD=27.0
  else
    COVERAGE_THRESHOLD=96.5
  fi
  COVERAGE_FAILED=$(echo "${COVERAGE_VALUE}<${COVERAGE_THRESHOLD}" | bc)
  if test ${COVERAGE_FAILED} -eq 1; then
      echo Code coverage ${COVERAGE_VALUE} is lower than limit of ${COVERAGE_THRESHOLD}
      exit 1
  else
      echo Code coverage ${COVERAGE_VALUE} is good and higher than limit of ${COVERAGE_THRESHOLD}
  fi
fi

# We want to allow per_file_coverage to fail without exiting this script.
set +e
if [[ "$VALIDATE_COVERAGE" == "true" ]]; then
  echo "Checking per-extension coverage"
  output=$(./test/per_file_coverage.sh)

  if [ $? -eq 1 ]; then
    echo Per-extension coverage failed:
    echo $output
    exit 1
  fi
  echo Per-extension coverage passed.
fi

echo "HTML coverage report is in ${COVERAGE_DIR}/index.html"
