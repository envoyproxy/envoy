#!/bin/bash

# directory:coverage_percent
# for existing extensions with low coverage.
declare -a KNOWN_LOW_COVERAGE=(
)

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"
COVERAGE_DIR="${SRCDIR}"/generated/coverage
COVERAGE_DATA="${COVERAGE_DIR}/coverage.dat"

FAILED=0
DEFAULT_COVERAGE_THRESHOLD=96.6
DIRECTORY_THRESHOLD=$DEFAULT_COVERAGE_THRESHOLD

# Unfortunately we have a bunch of preexisting extensions with low coverage.
# Set their low bar as their current coverage level.
get_coverage_target() {
  DIRECTORY_THRESHOLD=$DEFAULT_COVERAGE_THRESHOLD
  for FILE_PERCENT in ${KNOWN_LOW_COVERAGE[@]}
  do
    if [[ $FILE_PERCENT =~ "$1:" ]]; then
      DIRECTORY_THRESHOLD=$(echo $FILE_PERCENT | sed 's/.*://')
      return
    fi
  done
}

# Make sure that for each extension directory with code, coverage doesn't dip
# below the default coverage threshold.
for DIRECTORY in $(find source/extensions/* -type d)
do
  get_coverage_target $DIRECTORY
  COVERAGE_VALUE=$(lcov -e $COVERAGE_DATA  "$DIRECTORY/*" -o /dev/null | grep line |  cut -d ' ' -f 4)
  COVERAGE_VALUE=${COVERAGE_VALUE%?}
  # If the coverage number is 'n' (no data found) there is 0% coverage. This is
  # probably a directory without source code, so we skip checks.
  #
  # We could insist that we validate that 0% coverage directories are in a
  # documented list, but instead of adding busy-work for folks adding
  # non-source-containing directories, we trust reviewers to notice if there's
  # absolutely no tests for a full directory.
  if [[ $COVERAGE_VALUE =~ "n" ]]; then
    continue;
  fi;
  COVERAGE_FAILED=$(echo "${COVERAGE_VALUE}<${DIRECTORY_THRESHOLD}" | bc)
  if test ${COVERAGE_FAILED} -eq 1; then
    echo Code coverage for extension ${DIRECTORY} is lower than limit of ${DIRECTORY_THRESHOLD} \(${COVERAGE_VALUE}\)
    FAILED=1
  fi
done

exit $FAILED
