#!/bin/bash

RELEASE_VERSION=0.2.3

if [[ ! -d bazel-compilation-database-${RELEASE_VERSION} ]]; then
  curl -L https://github.com/grailbio/bazel-compilation-database/archive/${RELEASE_VERSION}.tar.gz | tar -xz
fi

bazel-compilation-database-${RELEASE_VERSION}/generate.sh $@
