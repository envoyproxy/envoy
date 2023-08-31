#!/bin/bash -E

TESTFILTER="${1:-*}"
TESTEXCLUDES="${2}"
FAILED=()
SRCDIR="${SRCDIR:-$(pwd)}"
WARNINGS=()

# Sandboxes listed here should be regarded as broken(/untested) until whatever
# is causing them to flake is resolved!!!
FLAKY_SANDBOXES=(
    # https://github.com/envoyproxy/envoy/issues/28542
    double-proxy
    # https://github.com/envoyproxy/envoy/issues/28541
    wasm-cc
    # https://github.com/envoyproxy/envoy/issues/28546
    websocket)


trap_errors () {
    local frame=0 command line sub file
    for flake in "${FLAKY_SANDBOXES[@]}"; do
        if [[ "$example" == "./${flake}" ]]; then
            WARNINGS+=("FAILED (${flake})")
            return
        fi
    done
    if [[ -n "$example" ]]; then
        command=" (${example})"
    fi
    set +v
    while read -r line sub file < <(caller "$frame"); do
        if [[ "$frame" -ne "0" ]]; then
            FAILED+=("  > ${sub}@ ${file} :${line}")
        else
            FAILED+=("${sub}@ ${file} :${line}${command}")
        fi
        ((frame++))
    done
    set -v
}

trap trap_errors ERR
trap exit 1 INT


run_examples () {
    local examples example
    cd "${SRCDIR}/examples" || exit 1

    examples=$(find . -mindepth 1 -maxdepth 1 -type d -name "$TESTFILTER" ! -iname "_*" | sort)
    if [[ -n "$TESTEXCLUDES" ]]; then
        examples=$(echo "$examples" | grep -Ev "$TESTEXCLUDES")
    fi
    for example in $examples; do
        pushd "$example" > /dev/null || return 1
        ./verify.sh
        popd > /dev/null || return 1
    done
}

run_examples

if [[ "${#WARNINGS[@]}" -ne "0" ]]; then
    for warning in "${WARNINGS[@]}"; do
        echo "WARNING: $warning" >&2
    done
fi

if [[ "${#FAILED[@]}" -ne "0" ]]; then
    echo "TESTS FAILED:"
    for failed in "${FAILED[@]}"; do
        echo "$failed" >&2
    done
    exit 1
fi
