#!/bin/bash

# We need versions.bzl for the build, but it's not in ci/build_container. Using Docker relative path
# workaround from https://github.com/docker/docker/issues/2745#issuecomment-253230025 to get this to
# work.
tar cf - . -C ../../bazel versions.bzl | docker build --rm -t lyft/envoy-build:$TAG -
