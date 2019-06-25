#!/bin/bash

#TODO(lizan): revert to released version once new version is released
#TODO(keith): revert after https://github.com/grailbio/bazel-compilation-database/pull/30 is resolved
RELEASE_VERSION=436399bfe9dbd6455d029722bff2948f2d7a1f04

if [[ ! -d bazel-compilation-database-${RELEASE_VERSION} ]]; then
  curl -L https://github.com/keith/bazel-compilation-database/archive/${RELEASE_VERSION}.tar.gz | tar -xz
fi

bazel-compilation-database-${RELEASE_VERSION}/generate.sh $@
