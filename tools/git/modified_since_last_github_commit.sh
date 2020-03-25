#!/bin/bash

declare -r BASE="$(dirname "$0")"
declare -r TARGET_PATH=$1
declare -r EXTENSION=$2

git diff --name-only $("${BASE}"/last_github_commit.sh)..HEAD | grep "\.${EXTENSION}$"
