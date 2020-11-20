#!/bin/bash -E

TESTFILTER="${1:-*}"
FAILED=()
SRCDIR="${SRCDIR:-$(pwd)}"
EXCLUDED_BUILD_CONFIGS=${EXCLUDED_BUILD_CONFIGS:-"^./jaeger-native-tracing|docker-compose"}


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
    examples=$(find . -mindepth 1 -maxdepth 1 -type d -name "$TESTFILTER" | sort)
    for example in $examples; do
        pushd "$example" > /dev/null || return 1
        ./verify.sh
        popd > /dev/null || return 1
    done
}

verify_build_configs () {
    local config configs missing
    missing=()
    cd "${SRCDIR}/examples" || return 1
    configs="$(find . -name "*.yaml" -o -name "*.lua" | grep -vE "${EXCLUDED_BUILD_CONFIGS}" | cut  -d/ -f2-)"
    for config in $configs; do
        grep "\"$config\"" BUILD || missing+=("$config")
    done
    if [[ -n "${missing[*]}" ]]; then
       for config in "${missing[@]}"; do
           echo "Missing config: $config" >&2
       done
       return 1
    fi
}

verify_build_configs
run_examples


if [[ "${#FAILED[@]}" -ne "0" ]]; then
    echo "TESTS FAILED:"
    for failed in "${FAILED[@]}"; do
        echo "$failed" >&2
    done
    exit 1
fi
