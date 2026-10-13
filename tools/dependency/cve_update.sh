#!/usr/bin/env bash


if [[ ! -e "$START_YEAR_PATH" ]]; then
    "START_YEAR_PATH env var must be set to an file path containing just a year" >&2
    exit 1
fi

start_year="$(cat "$START_YEAR_PATH")"
start_month="${start_year}-01"
current_month=$(date +"%Y-%m")

FETCH_ARGS=(
    --output_path="$CVE_DATA_PATH"
    --overwrite)

if [[ -e "${CVE_DATA_PATH}" && -z "${OVERWRITE_ALL_CVE_DATA}" ]]; then
    months=$(find "${CVE_DATA_PATH}" -maxdepth 1 -name "*.json" -printf "%f\n" \
        | sed 's/\.json$//' \
        | grep -v "^${current_month}$" \
        | paste -sd, -)
    if [[ -n "$months" ]]; then
        FETCH_ARGS+=(--skip "$months")
    fi
fi

if [[ ! -x "$FETCHER" ]]; then
    "FETCHER env var must be set to an executable"  >&2
    exit 1
fi

"$FETCHER" \
    "$start_month" \
    "$current_month" \
    "${FETCH_ARGS[@]}"
