#!/usr/bin/env bash

set -euo pipefail

########################################################################
# should_run_ci.sh
#
# Checks current branch and changed PR paths to determine if mobile CI
# jobs should be run.
########################################################################

job="$GITHUB_JOB"
branch_name="$GITHUB_REF_NAME"

function success() {
  echo "Running $job because there are $1 changes on $branch_name"
  echo "run_ci_job=true" >> "$GITHUB_OUTPUT"
}

function failure() {
  echo "Skipping $job because there are no mobile changes on $branch_name"
  echo "run_ci_job=false" >> "$GITHUB_OUTPUT"
}

# TODO(jpsim): Consider enabling this if the load on EngFlow is ok
# if [[ $branch_name == "main" ]]; then
#   # Run all mobile CI jobs on `main`
#   echo "Running $job because current branch is main"
#   echo "run_ci_job=true" >> "$GITHUB_OUTPUT"
#   exit 0
# fi

base_commit="$(git merge-base origin/main HEAD)"
changed_files="$(git diff "$base_commit" --name-only)"

if grep -q "^mobile/" <<< "$changed_files"; then
  success "mobile"
elif grep -q "^bazel/repository_locations\.bzl" <<< "$changed_files"; then
  success "bazel/repository_locations.bzl"
elif grep -q "^\.github/workflows/" <<< "$changed_files"; then
  success "GitHub Workflows"
else
  failure
fi
