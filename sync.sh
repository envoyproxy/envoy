#!/bin/bash

# Temporary script for synchronizing repos

rsync -av ../envoy-api/envoy ./api/
rsync -av ../envoy-api/bazel ./api/
rsync -av ../envoy-api/VERSION ./
rsync -av ../envoy-api/BUILD ./
rsync -av ../envoy-api/docs/{root,conf.py,requirements.txt} ./docs
rsync -av ../envoy-api/docs/BUILD ./api/docs
rsync -av ../envoy-api/tools/protodoc ./tools
rsync -av ../envoy-api/diagrams ./api
rsync -av ../envoy-api/*.md ./api
rsync -av ../envoy-api/test ./api

echo "// NOLINT(namespace-envoy)" | cat - ./api/test/validate/pgv_test.cc > /tmp/foo.txt
mv /tmp/foo.txt ./api/test/validate/pgv_test.cc
echo "// NOLINT(namespace-envoy)" | cat - ./api/test/build/build_test.cc > /tmp/foo.txt
mv /tmp/foo.txt ./api/test/build/build_test.cc

#ENVOY_DOCKER_BUILD_DIR=~/tmp/envoy-docker-build ./ci/run_envoy_docker.sh './ci/do_ci.sh fix_format'

#(cd api; 
# replace "\"//envoy/" "\"//api/envoy/";
# replace "//bazel:api_build_system.bzl" "//api/bazel:api_build_system.bzl";
# replace "@envoy_api//api" "//api")
#sed -i "s#load(\"//api#load(\"@envoy//api#" api/bazel/repositories.bzl

