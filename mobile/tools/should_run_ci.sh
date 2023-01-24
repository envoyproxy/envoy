#!/usr/bin/env bash

set -euo pipefail

########################################################################
# should_run_ci.sh
#
# Checks current branch and changed PR paths to determine if mobile CI
# jobs should be run.
########################################################################

if [[ $GITHUB_JOB == "swifttests" ]]; then
  echo "run_ci_job=true" >> "$GITHUB_OUTPUT"
else
  echo "run_ci_job=false" >> "$GITHUB_OUTPUT"
fi
