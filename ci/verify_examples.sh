#!/bin/bash -E

TESTFILTER="${1:-*}"
TESTEXCLUDES="${2}"
FAILED=()
SRCDIR="${SRCDIR:-$(pwd)}"

trap_errors () {
    local frame=0 command line sub file
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
    examples=$(find . -mindepth 1 -maxdepth 1 -type d -name "$TESTFILTER" ! -iname "_*" ! -name "$TESTEXCLUDES" | sort)
    for example in $examples; do
        pushd "$example" > /dev/null || return 1
        ./verify.sh
        popd > /dev/null || return 1
    done
}

run_examples

if [[ "${#FAILED[@]}" -ne "0" ]]; then
    echo "TESTS FAILED:"
    for failed in "${FAILED[@]}"; do
        echo "$failed" >&2
    done
    exit 1
fi
