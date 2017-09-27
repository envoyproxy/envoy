#!/bin/bash

set -e

# Build envoy and run tests as separate steps so that failure output
# is somewhat more deterministic (rather than interleaving the build
# and test steps).

bazel build --verbose_failures //source/...
bazel build --verbose_failures //test/...
bazel test --test_output=all //test/...
