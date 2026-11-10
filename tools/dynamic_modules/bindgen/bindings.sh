#!/usr/bin/env bash
# Checks (`check`) or regenerates (`fix`) the Rust SDK's checked-in ABI bindings with
# //tools/dynamic_modules/bindgen. Paths and the clang environment come from the BUILD file.

set -euo pipefail

mode="${1:?usage: bindings.sh check|fix}"
: "${BINDGEN:?}" "${ABI_HEADER:?}" "${ABI_BINDINGS:?}"

case "$mode" in
    check)
        generated="${TEST_TMPDIR:-${TMPDIR:-/tmp}}/abi.rs"
        "$BINDGEN" --header "$ABI_HEADER" --output "$generated"
        if ! diff -u "$ABI_BINDINGS" "$generated"; then
            echo >&2
            echo "ERROR: $ABI_BINDINGS is out of date with respect to $ABI_HEADER:" >&2
            echo "  run: bazel run //tools/dynamic_modules/bindgen:fix" >&2
            exit 1
        fi
        ;;
    fix)
        # `bazel run` starts in the runfiles tree; write into the source tree.
        workspace="${BUILD_WORKSPACE_DIRECTORY:?run with bazel run}"
        exec "$BINDGEN" --header "$ABI_HEADER" --output "$workspace/$ABI_BINDINGS"
        ;;
    *)
        echo "unknown mode: $mode" >&2
        exit 2
        ;;
esac
