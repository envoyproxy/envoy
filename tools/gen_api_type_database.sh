#!/bin/bash

set -e

# Clean up any stale files in the API tree output. Bazel remembers valid cached
# files still.
rm -rf bazel-bin/external/envoy_api

# TODO(htuch): This script started life by cloning docs/build.sh. It depends on
# the @envoy_api//docs:protos target in a few places as a result. This is not
# guaranteed to be the precise set of protos we want to format, but as a
# starting place it seems reasonable. In the future, we should change the logic
# here.
bazel build ${BAZEL_BUILD_OPTIONS} --//tools/api_proto_plugin:default_type_db_target=@envoy_api//docs:protos \
  @envoy_api//docs:protos --aspects //tools/protoxform:protoxform.bzl%protoxform_aspect --output_groups=proto \
  --action_env=CPROFILE_ENABLED=1 --host_force_python=PY3
