#!/usr/bin/env bash
# Asserts that every envoy_dynamic_module_callback_* in the ABI is exported from
# envoy-static when the callback-defining translation units are built with
# -fvisibility=hidden (abi_exports.bzl). envoy-static links every registered extension and
# exports the bazel/exported_symbols.txt patterns via --dynamic-list; a callback definition
# compiled hidden cannot be re-exported, leaving dlopen'd modules unable to resolve it. The
# expected callback list comes from //bazel:exported_symbols_windows, which
# //tools/windows:gen_dynamic_modules_exports extracts from abi.h.
#
# A failure means the callback's declaration in abi.h is missing its
# ENVOY_DYNAMIC_MODULE_CALLBACK_EXPORT annotation.

set -euo pipefail
: "${CALLBACK_DEF:?}" "${ABI_EXPORTS_TEST_BIN:?}"

# The env values are $(rootpath)-relative; resolve them against the runfiles tree.
RUNFILES="${TEST_SRCDIR:?}/${TEST_WORKSPACE:?}"
CALLBACK_DEF="$RUNFILES/$CALLBACK_DEF"
ABI_EXPORTS_TEST_BIN="$RUNFILES/$ABI_EXPORTS_TEST_BIN"

# llvm-readelf comes from the hermetic LLVM toolchain (set in the BUILD file); fall back to
# readelf from PATH on non-clang builds.
READELF="${READELF:-readelf}"
if [[ "$READELF" == */* ]]; then
    READELF="$RUNFILES/$READELF"
fi

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

awk 'NR > 1 {print $1}' "$CALLBACK_DEF" | sort -u > "$workdir/expected"

"$READELF" --dyn-syms -W "$ABI_EXPORTS_TEST_BIN" \
    | awk '$4 == "FUNC" && $7 != "UND" {print $8}' | sort -u > "$workdir/exported"

missing="$(comm -23 "$workdir/expected" "$workdir/exported")"
if [[ -n "$missing" ]]; then
    echo "ERROR: callbacks not exported from $ABI_EXPORTS_TEST_BIN when built with" >&2
    echo "-fvisibility=hidden:" >&2
    echo "$missing" >&2
    echo >&2
    echo "Annotate the declaration in abi.h with ENVOY_DYNAMIC_MODULE_CALLBACK_EXPORT." >&2
    exit 1
fi

echo "all $(wc -l < "$workdir/expected") callbacks exported with -fvisibility=hidden"
