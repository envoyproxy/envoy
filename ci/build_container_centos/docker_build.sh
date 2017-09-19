#!/bin/bash

# Using Docker relative path workaround from
# https://github.com/docker/docker/issues/2745#issuecomment-253230025
tar cf - -C ../build_container . \
         -C ../../bazel target_recipes.bzl \
         -C ../ci/build_container_centos . | \
    docker build --rm -t lyft/envoy-build-centos:$TRAVIS_COMMIT -
