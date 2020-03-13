#!/bin/bash

# Dummy shell implementation for nooping tests.
# TODO(lizan): remove when we have a solution for
# https://github.com/bazelbuild/bazel/issues/3510

cd $(dirname "$0")

"$@"
