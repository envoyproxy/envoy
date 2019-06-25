#!/bin/bash

#TODO(lizan): revert to released version once new version is released
#TODO(keith): revert after https://github.com/grailbio/bazel-compilation-database/pull/30 is resolved
RELEASE_VERSION=e65a17bef735ef64457221800207f6c295a6c8aa

if [[ ! -d bazel-compilation-database-${RELEASE_VERSION} ]]; then
  curl -L https://github.com/keith/bazel-compilation-database/archive/${RELEASE_VERSION}.tar.gz | tar -xz
fi

bazel-compilation-database-${RELEASE_VERSION}/generate.sh $@
