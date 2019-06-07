#!/usr/bin/env bash

set -e

export BUILDIFIER_BIN="/usr/local/bin/buildifier"

ENVOY_FORMAT_ACTION="$1"
if [ -z "$ENVOY_FORMAT_ACTION" ]; then
  echo "No action specified, defaulting to check"
  ENVOY_FORMAT_ACTION="check"
fi

# TODO(mattklein123): WORKSPACE is excluded due to warning about @bazel_tools reference. Fix here
#                     or in the upstream checker.
# TODO(mattklein123): Objective-C is excluded because the clang-format setup is not correct. Fix.
# TODO(mattklein123): We don't need envoy_package() in various files. Somehow fix in upstream
#                     checker.
envoy/tools/check_format.py \
    --add-excluded-prefixes ./envoy/ ./envoy_build_config/extensions_build_config.bzl ./WORKSPACE ./examples/objective-c/ \
    --skip_envoy_build_rule_check "$ENVOY_FORMAT_ACTION"
envoy/tools/format_python_tools.sh check
