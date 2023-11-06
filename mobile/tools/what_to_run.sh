#!/bin/bash

set -euo pipefail

BRANCH_NAME="$GITHUB_REF_NAME"
BASE_COMMIT="$(git merge-base origin/main HEAD)"
CHANGED_FILES="$(git diff "${BASE_COMMIT}" --name-only)"
CHANGE_MATCH='^mobile/|^bazel/repository_locations\.bzl|^\.bazelrc|^\.bazelversion|^\.github/workflows/mobile-*|^\.github/workflows/_env.yml|^tools/code_format/check_format.py|bazel/external/quiche.BUILD'

# The logic in this file is roughly:
#
#    pull_request + changed files = run all mobile CI
#
#    main = run some mobile CI
#
#    all other commits = run minimal mobile CI (these jobs have no conditions)
#
# Branches are not currently tested, altho that should be restricted by the workflow


run_default_ci () {
    {
        echo "mobile_android_build=true"
        echo "mobile_android_tests=true"
        echo "mobile_asan=true"
        echo "mobile_cc_tests=true"
        echo "mobile_compile_time_options=true"
        echo "mobile_coverage=true"
        echo "mobile_formatting=true"
        echo "mobile_ios_build=true"
        echo "mobile_ios_tests=true"
        echo "mobile_release_validation=true"
        echo "mobile_tsan=true"
    } >> "$GITHUB_OUTPUT"
}

run_ci_for_changed_files () {
    run_default_ci
    {
        echo "mobile_android_build_all=true"
        echo "mobile_ios_build_all=true"
    } >> "$GITHUB_OUTPUT"
}

if [[ "$BRANCH_NAME" == "main" ]]; then
    run_default_ci
    exit 0
fi

if grep -qE "$CHANGE_MATCH" <<< "$CHANGED_FILES"; then
    run_ci_for_changed_files
fi
