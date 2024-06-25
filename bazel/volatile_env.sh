#!/usr/bin/env bash

set -e

if [[ ! -f bazel-out/volatile-status.txt ]]; then
    # shellcheck disable=SC2016
    echo 'No `bazel-out/volatile-status.txt`, did you forget to stamp your build target?' >&2
    exit 1
fi

VOLATILE=$(cat bazel-out/volatile-status.txt)

while read -r line ; do
    export "$(echo "${line}" | cut -d' ' -f 1)=$(echo "${line}" | cut -d' ' -f 2)"
done <<< "$VOLATILE"
