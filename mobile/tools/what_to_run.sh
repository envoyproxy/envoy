#!/bin/bash

set -euo pipefail

branch_name="$GITHUB_REF_NAME"


load_defaults () {
    {
        echo "android_build=true"
        echo "android_tests=true"
        echo "asan=true"
        echo "cc_tests=true"
        echo "compile_time_options=true"
        echo "coverage=true"
        echo "formatting=true"
        echo "ios_build=true"
        echo "ios_tests=true"
        echo "perf=true"
        echo "release_validation=true"
        echo "tsan=true"
    } >> "$GITHUB_OUTPUT"
}

function success() {
    load_defaults
    {
        echo "android_build_all=true"
        echo "ios_build_all=true"
    } >> "$GITHUB_OUTPUT"
}

if [[ $branch_name == "main" ]]; then
    load_defaults
    exit 0
fi

base_commit="$(git merge-base origin/main HEAD)"
changed_files="$(git diff "$base_commit" --name-only)"

if grep -q "^mobile/" <<< "$changed_files"; then
    success "mobile"
elif grep -q "^bazel/repository_locations\.bzl" <<< "$changed_files"; then
    success "bazel/repository_locations.bzl"
elif grep -q "^\.bazelrc" <<< "$changed_files"; then
    success ".bazelrc"
elif grep -q "^\.github/workflows/mobile-*" <<< "$changed_files"; then
    success "GitHub Workflows"
fi
