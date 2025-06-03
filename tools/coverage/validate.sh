#!/bin/bash
set -eo pipefail

COVERAGE_JSON="${1}"
FUZZ_COVERAGE="${2}"
IS_MOBILE="${3}"

if [[ -z "$COVERAGE_JSON" || ! -f "$COVERAGE_JSON" ]]; then
    echo "ERROR: Coverage JSON file not provided or not found: $COVERAGE_JSON" >&2
    exit 1
fi

if [[ -z "$COVERAGE_CONFIG" || ! -f "$COVERAGE_CONFIG" ]]; then
    echo "ERROR: Coverage config file not found: $COVERAGE_CONFIG" >&2
    exit 1
fi

THRESHOLD_REACHED="$($JQ_BIN '.summary.threshold_reached' "$COVERAGE_JSON")"
SUMMARY_MESSAGE="$($JQ_BIN -r '.summary_message' "$COVERAGE_JSON")"

if [[ "${FUZZ_COVERAGE}" == "true" || "${IS_MOBILE}" == "true" ]]; then
    echo "$SUMMARY_MESSAGE"
    if [[ "${THRESHOLD_REACHED}" == "false" ]]; then
        exit 1
    fi
    exit 0
fi

FAILED_COUNT=$($JQ_BIN '.validation_summary.failed_count' "$COVERAGE_JSON")
ADJUSTABLE_COUNT=$($JQ_BIN '.validation_summary.adjustable_count' "$COVERAGE_JSON")
if [[ "$FAILED_COUNT" -gt 0 || "$ADJUSTABLE_COUNT" -gt 0 ]]; then
    $JQ_BIN -r '.summary_report' "$COVERAGE_JSON"
else
    echo "$SUMMARY_MESSAGE"
fi
if [[ "$FAILED_COUNT" -gt 0 || "${THRESHOLD_REACHED}" == "false" ]]; then
    exit 1
fi
exit 0
