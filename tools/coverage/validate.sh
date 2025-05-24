#!/bin/bash
set -eo pipefail

COVERAGE_JSON="${1}"
COVERAGE_THRESHOLD="${2}"
FUZZ_COVERAGE="${3}"
IS_MOBILE="${4}"


if [[ -z "$COVERAGE_JSON" || ! -f "$COVERAGE_JSON" ]]; then
    echo "ERROR: Coverage JSON file not provided or not found: $COVERAGE_JSON" >&2
    exit 1
fi

if [[ -z "$COVERAGE_CONFIG" || ! -f "$COVERAGE_CONFIG" ]]; then
    echo "ERROR: Coverage config file not found: $COVERAGE_CONFIG" >&2
    exit 1
fi

RETURNS=0
COVERAGE_VALUE="$($JQ_BIN '.summary.coverage_percent' "$COVERAGE_JSON")"
COVERAGE_FAILED=$(echo "${COVERAGE_VALUE}<${COVERAGE_THRESHOLD}" | bc)
if [[ "${COVERAGE_FAILED}" -eq 1 ]]; then
    echo "ERROR: Code coverage ${COVERAGE_VALUE} is lower than limit of ${COVERAGE_THRESHOLD}" >&2
    RETURNS=1
else
    echo "Code coverage ${COVERAGE_VALUE} is good and higher than limit of ${COVERAGE_THRESHOLD}"
fi
if [[ "${FUZZ_COVERAGE}" == "true" || "${IS_MOBILE}" == "true" ]]; then
    exit "$RETURNS"
fi
DEFAULT_COVERAGE_THRESHOLD=96.6
FAILED=0
echo "Checking per-directory coverage..."
echo

# Find all directories containing .cc files in the source tree
DIRECTORIES=$(find "${BUILD_WORKING_DIRECTORY}/source" -name "*.cc" -type f -printf '%h\n' | sed "s|^${BUILD_WORKING_DIRECTORY}/||" | sort -u)

declare -a FAILED_COVERAGE=()
declare -a LOW_COVERAGE=()
declare -a EXCELLENT_COVERAGE=()
declare -a HIGH_COVERAGE_ADJUSTABLE=()

while IFS= read -r DIRECTORY; do
    [[ -z "$DIRECTORY" ]] && continue
    COVERAGE_VALUE=$(${JQ_BIN} -r ".per_directory_coverage[\"${DIRECTORY}\"].coverage_percent // \"n\"" "$COVERAGE_JSON")
    if [[ "$COVERAGE_VALUE" == "n" ]]; then
        continue
    fi
    COVERAGE_THRESHOLD=$(${JQ_BIN} -r ".\"${DIRECTORY}\" // ${DEFAULT_COVERAGE_THRESHOLD}" "$COVERAGE_CONFIG")
    if (( $(echo "$COVERAGE_VALUE < $COVERAGE_THRESHOLD" | bc -l) )); then
        # Coverage is below the threshold for this directory
        FAILED_COVERAGE+=("${DIRECTORY}: ${COVERAGE_VALUE}% (threshold: ${COVERAGE_THRESHOLD}%)")
        FAILED=1
    else
        # Coverage meets or exceeds the threshold
        if (( $(echo "$COVERAGE_THRESHOLD < $DEFAULT_COVERAGE_THRESHOLD" | bc -l) )); then
            # This directory has a configured exception (lower threshold)
            LOW_COVERAGE+=("${DIRECTORY}: ${COVERAGE_VALUE}% (configured threshold: ${COVERAGE_THRESHOLD}%)")

            # Check if coverage is significantly higher than the configured threshold
            if (( $(echo "$COVERAGE_VALUE > $COVERAGE_THRESHOLD" | bc -l) )); then
                HIGH_COVERAGE_ADJUSTABLE+=("${DIRECTORY}: ${COVERAGE_VALUE}% (current threshold: ${COVERAGE_THRESHOLD}%)")
            fi
        elif (( $(echo "$COVERAGE_VALUE >= $DEFAULT_COVERAGE_THRESHOLD" | bc -l) )); then
            # Excellent coverage (meets default threshold)
            EXCELLENT_COVERAGE+=("${DIRECTORY}: ${COVERAGE_VALUE}%")
        fi
    fi
done <<< "$DIRECTORIES"

echo "================== Per-Directory Coverage Report =================="
echo

if [[ ${#EXCELLENT_COVERAGE[@]} -gt 0 ]]; then
    echo "Directories with excellent coverage (>=${DEFAULT_COVERAGE_THRESHOLD}%):"
    for dir in "${EXCELLENT_COVERAGE[@]}"; do
        echo "  ✓ $dir"
    done
    echo
fi

if [[ ${#LOW_COVERAGE[@]} -gt 0 ]]; then
    echo "Directories with known low coverage (meeting configured thresholds):"
    for dir in "${LOW_COVERAGE[@]}"; do
        echo "  ⚠ $dir"
    done
    echo
fi

if [[ ${#HIGH_COVERAGE_ADJUSTABLE[@]} -gt 0 ]]; then
    echo "WARNING: Coverage in the following directories may be adjusted up:"
    for dir in "${HIGH_COVERAGE_ADJUSTABLE[@]}"; do
        echo "  ⬆ $dir"
    done
    echo
fi

if [[ ${#FAILED_COVERAGE[@]} -gt 0 ]]; then
    echo "FAILED: Directories not meeting coverage thresholds:"
    for dir in "${FAILED_COVERAGE[@]}"; do
        echo "  ✗ $dir"
    done
    echo
fi

TOTAL_COVERAGE=$(${JQ_BIN} -r '.summary.coverage_percent' "$COVERAGE_JSON")
SOURCE_DIR_COUNT=$(echo "$DIRECTORIES" | wc -l)
echo "=================================================================="
echo "Overall Coverage: ${TOTAL_COVERAGE}%"
echo "Source directories checked: ${SOURCE_DIR_COUNT}"
echo "Failed: ${#FAILED_COVERAGE[@]}"
echo "Low coverage (configured): ${#LOW_COVERAGE[@]}"
echo "Excellent coverage: ${#EXCELLENT_COVERAGE[@]}"
if [[ ${#HIGH_COVERAGE_ADJUSTABLE[@]} -gt 0 ]]; then
    echo "Can be adjusted up: ${#HIGH_COVERAGE_ADJUSTABLE[@]}"
fi
echo "=================================================================="

if [[ $FAILED -eq 1 ]]; then
    echo
    echo "ERROR: Coverage check failed. Some directories are below their thresholds."
    exit 1
fi

exit "$RETURNS"
