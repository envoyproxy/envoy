#!/usr/bin/env bash

set -eo pipefail

CONFIGGEN="$1"
shift
TARGETFILE="$1"
shift
OUT_DIR="$1"
shift


TARGETFILE="$(realpath "$TARGETFILE")"

mkdir -p "$OUT_DIR/certs"
mkdir -p "$OUT_DIR/lib"
mkdir -p "$OUT_DIR/protos"

if [[ "$CONFIGGEN" != "NO_CONFIGGEN" ]]; then
    "$CONFIGGEN" "$OUT_DIR"
fi

for FILE in "$@"; do
    case "$FILE" in
        *.pem|*.der)
            cp "$FILE" "$OUT_DIR/certs"
            ;;
        *.lua|*.wasm|*.so)
            cp "$FILE" "$OUT_DIR/lib"
            ;;
        *.pb)
            cp "$FILE" "$OUT_DIR/protos"
            ;;
        *)
            FILENAME="$(echo "$FILE" | sed -e 's/.*examples\///g')"
            # Configuration filenames may conflict. To avoid this we use the full path.
            cp "$FILE" "$OUT_DIR/${FILENAME//\//_}"
            ;;
    esac
done


cd "$OUT_DIR" || exit 1

files=()
for pattern in *.yaml certs/*.pem certs/*.der protos/*.pb lib/*.so lib/*.wasm lib/*.lua; do
    for file in $pattern; do
        if [[ -e "$file" ]]; then
            files+=("$file")
        fi
    done
done

if (( ${#files[@]} > 0 )); then
    tar -hcf "$TARGETFILE" -- "${files[@]}"
fi
