#!/bin/bash

RELEASE_VERSION=0.2.3

if [[ ! -d bazel-compilation-database-${RELEASE_VERSION} ]]; then
  curl -L https://github.com/grailbio/bazel-compilation-database/archive/${RELEASE_VERSION}.tar.gz | tar -xz
fi

bazel-compilation-database-${RELEASE_VERSION}/generate.sh $@

OUTPUT_BASE=$(bazel info output_base)

# Workaround for envoy_cc_wrapper
sed -i "s|${OUTPUT_BASE}/external/local_config_cc/extra_tools/envoy_cc_wrapper|clang++|" compile_commands.json
sed -i "s|-std=c++0x ||" compile_commands.json
