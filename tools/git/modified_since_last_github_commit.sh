#!/usr/bin/env bash

BASE="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"
declare -r BASE
declare -r TARGET_PATH=$1
declare -r EXTENSION=$2
export TARGET_PATH


"${GIT_BIN:-git}" diff --name-only "$("${BASE}"/last_github_commit.sh)" | grep "\.${EXTENSION}$"
