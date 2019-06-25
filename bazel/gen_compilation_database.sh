#!/bin/bash

#TODO(lizan): revert to released version once new version is released
#TODO(keith): revert after https://github.com/grailbio/bazel-compilation-database/pull/30 is resolved
RELEASE_VERSION=078319b6a2ed88915d817d4a4142e1adfb77d727

if [[ ! -d bazel-compilation-database-${RELEASE_VERSION} ]]; then
  curl -L https://github.com/keith/bazel-compilation-database/archive/${RELEASE_VERSION}.tar.gz | tar -xz
fi

bazel-compilation-database-${RELEASE_VERSION}/generate.sh $@
