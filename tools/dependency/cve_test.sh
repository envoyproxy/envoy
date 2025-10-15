#!/usr/bin/env bash

set -e

ANSI_LIBDIR="$(dirname "$JQ_ANSI_UTILS")"
CVE_LIBDIR="$(dirname "$JQ_CVE_UTILS")"
VERSION_LIBDIR="$(dirname "$JQ_VERSION_UTILS")"

# Check if the JSON array contains any CVEs and not just if file is non-empty.
CVE_COUNT=$("$JQ_BIN" 'length' "$1")
if [[ "$CVE_COUNT" -gt 0 ]]; then
    "$JQ_BIN" -r -f \
         -L "$ANSI_LIBDIR" \
         -L "$CVE_LIBDIR" \
         -L "$VERSION_LIBDIR" \
         "$JQ_REPORT" \
         "$1"
    exit 1
fi
