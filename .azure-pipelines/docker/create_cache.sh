#!/bin/bash -e

set -o pipefail

CACHE_TARBALL="${1}"
ROOT_DIR="${2}"
shift 2

echo "Exporting ${*} -> ${CACHE_TARBALL}"

CACHE_PATH="$(dirname "$CACHE_TARBALL")"
mkdir -p "$CACHE_PATH"

CACHE_ARGS=()
for path in "$@"; do
    if [[ "$ROOT_DIR" == "." ]]; then
        total="$(du -sh "$path" | cut -f1)"
        echo "Adding cache dir (${path}): ${total}"
        CACHE_ARGS+=(-C "$path" .)
    else
        total="$(du -sh "${ROOT_DIR}/$path" | cut -f1)"
        echo "Adding cache dir (${ROOT_DIR}/${path}): ${total}"
        CACHE_ARGS+=(-C "$ROOT_DIR" "$path")
    fi
done

tar cf - "${CACHE_ARGS[@]}" | zstd - -q -T0 -o "$CACHE_TARBALL"
echo "Cache tarball created: ${CACHE_TARBALL}"
ls -lh "$CACHE_TARBALL"
