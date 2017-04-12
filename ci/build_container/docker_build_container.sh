#!/bin/bash

cp ../../bazel/recipes.bzl .
docker build --rm -t lyft/envoy-build:$TAG .
