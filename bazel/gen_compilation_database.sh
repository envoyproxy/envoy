#!/bin/bash

#TODO(lizan): revert to released version once new version is released
RELEASE_VERSION=fd92bf39666596cab609edf7b8e221307d6d325a

if [[ ! -d bazel-compilation-database-${RELEASE_VERSION} ]]; then
  curl -L https://github.com/grailbio/bazel-compilation-database/archive/${RELEASE_VERSION}.tar.gz | tar -xz
fi

bazel-compilation-database-${RELEASE_VERSION}/generate.sh $@
