#!/usr/bin/env bash

# Dummy shell implementation for nooping tests.
# TODO(lizan): remove when we have a solution for
# https://github.com/bazelbuild/bazel/issues/3510

cd "$(realpath "$(dirname "${BASH_SOURCE[0]}")")" || exit 1

if [ $# -gt 0 ]; then
  "./${1}" "${@:2}"
fi
