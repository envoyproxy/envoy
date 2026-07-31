#!/usr/bin/env bash

# Bazel expects the helper to read stdin.
# See https://github.com/bazelbuild/bazel/pull/17666
cat /dev/stdin > /dev/null

# `GITHUB_TOKEN` is provided as a secret.
echo "{\"headers\":{\"Authorization\":[\"Bearer ${GITHUB_TOKEN}\"]}}"
