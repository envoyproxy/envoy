#!/bin/bash -e

set -o pipefail

CACHE_TARBALL="${1}"
shift

echo "Exporting ${*} -> ${CACHE_TARBALL}"

CACHE_PATH="$(dirname "$CACHE_TARBALL")"
mkdir -p "$CACHE_PATH"

CACHE_ARGS=()
for path in "$@"; do
    total="$(du -sh "$path" | cut -f1)"
    echo "Adding cache dir (${path}): ${total}"
    CACHE_ARGS+=(-C "$path" .)
done

tar cf - "${CACHE_ARGS[@]}" | zstd - -q -T0 -o "$CACHE_TARBALL"
echo "Cache tarball created: ${CACHE_TARBALL}"
ls -lh "$CACHE_TARBALL"
