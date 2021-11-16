#!/bin/bash -E

# Pre-checks for validation and linting
#
# These checks do not provide a fix and are quicker to run,
# allowing CI to fail quickly on basic linting or validation errors

FAILED=()
CURRENT=""
# AZP appears to make lines with this prefix red
BASH_ERR_PREFIX="##[error]: "

DIFF_OUTPUT="${DIFF_OUTPUT:-/build/fix_format_pre.diff}"

read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTIONS:-}"


trap_errors () {
    local frame=0 command line sub file
    if [[ -n "$CURRENT" ]]; then
        command=" (${CURRENT})"
    fi
    set +v
    while read -r line sub file < <(caller "$frame"); do
        if [[ "$frame" -ne "0" ]]; then
            FAILED+=("  > ${sub}@ ${file} :${line}")
        else
            FAILED+=("${sub}@ ${file} :${line}${command}")
            if [[ "$CURRENT" == "glint" ]]; then
                FAILED+=(
                    "    Please fix your editor to ensure:"
                    "      - no trailing whitespace"
                    "      - no mixed tabs/spaces"
                    "      - all files end with a newline")
            fi
        fi
        ((frame++))
    done
    set -v
}

trap trap_errors ERR
trap exit 1 INT

# TODO: move these to bazel
CURRENT=glint
"${ENVOY_SRCDIR}"/tools/code_format/glint.sh

CURRENT=shellcheck
"${ENVOY_SRCDIR}"/tools/code_format/check_shellcheck_format.sh check

CURRENT=configs
bazel run "${BAZEL_BUILD_OPTIONS[@]}" //configs:example_configs_validation

CURRENT=python
bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/code_format:python_check -- --diff-file="$DIFF_OUTPUT" --fix

CURRENT=extensions
bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/extensions:extensions_check

CURRENT=spelling
"${ENVOY_SRCDIR}"/tools/spelling/check_spelling_pedantic.py --mark check

CURRENT=rst
# TODO(phlax): Move this to general docs checking of all rst files
bazel run "${BAZEL_BUILD_OPTIONS[@]}" //tools/docs:rst_check

if [[ "${#FAILED[@]}" -ne "0" ]]; then
    echo "${BASH_ERR_PREFIX}TESTS FAILED:" >&2
    for failed in "${FAILED[@]}"; do
        echo "${BASH_ERR_PREFIX} $failed" >&2
    done
    exit 1
fi
