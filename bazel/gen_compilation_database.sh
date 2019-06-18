#!/bin/bash

#TODO(lizan): revert to released version once new version is released
RELEASE_VERSION=d5a0ee259aa356886618eafae17ca05ebf79d6c2

if [[ ! -d bazel-compilation-database-${RELEASE_VERSION} ]]; then
  curl -L https://github.com/grailbio/bazel-compilation-database/archive/${RELEASE_VERSION}.tar.gz | tar -xz
fi

bazel-compilation-database-${RELEASE_VERSION}/generate.sh $@
