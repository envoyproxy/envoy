#!/usr/bin/env bash

set -e

ENVOY_FORMAT_ACTION="$1"
if [ -z "$ENVOY_FORMAT_ACTION" ]; then
  echo "No action specified, defaulting to check"
  ENVOY_FORMAT_ACTION="check"
fi

TARGET_PATH="$2"

# TODO(mattklein123): WORKSPACE is excluded due to warning about @bazel_tools reference. Fix here
#                     or in the upstream checker.
ENVOY_BAZEL_PREFIX=@envoy envoy/tools/code_format/check_format.py \
    --add-excluded-prefixes ./envoy/ ./envoy_build_config/extensions_build_config.bzl ./WORKSPACE ./dist/Envoy.framework/ ./library/common/config_template.cc ./bazel/envoy_mobile_swift_bazel_support.bzl ./bazel/envoy_mobile_repositories.bzl \
    --skip_envoy_build_rule_check "$ENVOY_FORMAT_ACTION" $TARGET_PATH \
    --namespace_check_excluded_paths ./examples/ ./library/java/ ./library/kotlin ./library/objective-c \
    --build_fixer_check_excluded_paths ./BUILD ./dist ./examples ./library/java ./library/kotlin ./library/objective-c ./library/swift ./library/common/extensions
