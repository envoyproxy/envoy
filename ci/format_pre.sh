#!/usr/bin/env bash

set -E

# Pre-checks for validation and linting
#
# These checks do not provide a fix and are quicker to run,
# allowing CI to fail quickly on basic linting or validation errors

FAILED=()
CURRENT=""
# AZP appears to make lines with this prefix red
BASH_ERR_PREFIX="##[error]: "

DIFF_OUTPUT="${DIFF_OUTPUT:-/build/fix_format.diff}"

read -ra BAZEL_STARTUP_OPTIONS <<< "${BAZEL_STARTUP_OPTION_LIST:-}"
read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTION_LIST:-}"


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
            if [[ "$CURRENT" == "check" ]]; then
                # shellcheck disable=SC2016
                FAILED+=(
                    ""
                    '   *Code formatting check failed*: please search above logs for `CodeChecker ERROR`'
                    "")
            fi
        fi
        ((frame++))
    done
    set -v
}

trap trap_errors ERR
trap exit 1 INT


CURRENT=check
# This test runs code check with:
#   bazel run //tools/code:check -- --fix -v warn -x mobile/dist/envoy-pom.xml
# see: /tools/code/BUILD
bazel "${BAZEL_STARTUP_OPTIONS[@]}" test "${BAZEL_BUILD_OPTIONS[@]}" //tools/code:check_test

CURRENT=configs
bazel "${BAZEL_STARTUP_OPTIONS[@]}" run "${BAZEL_BUILD_OPTIONS[@]}" //configs:example_configs_validation

CURRENT=spelling
"${ENVOY_SRCDIR}/tools/spelling/check_spelling_pedantic.py" --mark check

CURRENT=check_format
bazel "${BAZEL_STARTUP_OPTIONS[@]}" run "${BAZEL_BUILD_OPTIONS[@]}" //tools/code_format:check_format -- fix --fail_on_diff

if [[ "${#FAILED[@]}" -ne "0" ]]; then
    echo "${BASH_ERR_PREFIX}TESTS FAILED:" >&2
    for failed in "${FAILED[@]}"; do
        echo "${BASH_ERR_PREFIX} $failed" >&2
    done
    if [[ $(git status --porcelain) ]]; then
        git diff > "$DIFF_OUTPUT"
        echo >&2
        echo "Applying the following diff should fix (some) problems" >&2
        echo >&2
        cat "$DIFF_OUTPUT" >&2
        echo >&2
        echo "Diff file with (some) fixes will be uploaded. Please check the artefacts for this PR run in the azure pipeline." >&2
        echo >&2
    fi
    exit 1
fi
