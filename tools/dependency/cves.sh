#!/usr/bin/env bash

JQ="$(realpath "$JQ_BIN")"
CPE_DEPS="$(realpath "$CPE_DEPS")"
JQ_CVE_UTILS="$(realpath "$JQ_CVE_UTILS")"
JQ_CVE_MATCHER="$(realpath "$JQ_CVE_MATCHER")"

if [[ ! -e "$JQ" || ! -x "$JQ" ]]; then
    echo "jq binary not found or not executable" >&2
    exit 1
fi
if [[ ! -e "$CPE_DEPS" ]]; then
    echo "CVE dependency JSON not found" >&2
    exit 1
fi
if [[ ! -e "$JQ_CVE_UTILS" ]]; then
    echo "CVE jq utils not found" >&2
    exit 1
fi
if [[ ! -e "$JQ_CVE_MATCHER" ]]; then
    echo "CVE jq matcher not found" >&2
    exit 1
fi
if [[ ! -e "$JQ_VERSION_UTILS" ]]; then
    echo "Version jq utils not found" >&2
    exit 1
fi

JQ_CVE_LIBDIR="$(dirname "$JQ_CVE_UTILS")"
JQ_VERSION_LIBDIR="$(dirname "$JQ_VERSION_UTILS")"

read -ra CVES <<< "$CVES"

for f in "${CVES[@]}"; do
    if [[ "$f" == *.json ]]; then
        HAS_JSON=true
        break
    fi
done
if [[ "$HAS_JSON" != true ]]; then
    echo "No CVE data set, perhaps use --config=cves?" >&2
    exit 1
fi

parse_cves () {
    # Stream the cves checking against the deps and then slurp the results into a single json object
    # cat "${CVEPATH}/"*.json \
    cat "${CVES[@]}" \
        | "$JQ" -f "$JQ_CVE_MATCHER" \
                -L "$JQ_CVE_LIBDIR" \
                -L "$JQ_VERSION_LIBDIR" \
                --slurpfile ignored "$CVES_IGNORED" \
                --slurpfile deps "$CPE_DEPS" \
        | "$JQ" -s '[.[][]]' \
        > found_cves.json
}

parse_cves
"$JQ" "." found_cves.json
