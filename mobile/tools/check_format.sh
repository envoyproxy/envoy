#!/usr/bin/env bash

set -e

ENVOY_FORMAT_ACTION="$1"
if [ -z "$ENVOY_FORMAT_ACTION" ]; then
  echo "No action specified, defaulting to check"
  ENVOY_FORMAT_ACTION="check"
fi

if [[ $(uname) == "Darwin" ]]; then
  if [[ "${ENVOY_FORMAT_ACTION}" == "fix" ]]; then
    ./bazelw run @SwiftLint//:swiftlint -- --fix --quiet 2>/dev/null
    ./bazelw run @DrString//:drstring format 2>/dev/null
  else
    ./bazelw run @SwiftLint//:swiftlint -- --strict --quiet 2>/dev/null
    ./bazelw run @DrString//:drstring check 2>/dev/null
  fi
fi

TARGET_PATH="$2"

# TODO(mattklein123): WORKSPACE is excluded due to warning about @bazel_tools reference. Fix here
#                     or in the upstream checker.

FORMAT_ARGS=(
    --config_path ../tools/code_format/config.yaml
    --add-excluded-prefixes
    ./envoy/ ./envoy_build_config/extensions_build_config.bzl ./WORKSPACE
    ./Envoy.xcodeproj/ ./dist/
    ./bazel/envoy_mobile_swift_bazel_support.bzl
    ./bazel/envoy_mobile_repositories.bzl
    ./examples/swift/swiftpm/Packages/Envoy.xcframework ./tmp
    --skip_envoy_build_rule_check)
if [[ -n "$TARGET_PATH" ]]; then
    FORMAT_ARGS+=("$TARGET_PATH")
fi
FORMAT_ARGS+=(
    --namespace_check_excluded_paths
    ./envoy ./examples/ ./library/java/ ./library/kotlin
    ./library/objective-c ./test/java ./test/java
    ./test/objective-c ./test/swift ./experimental/swift
    --build_fixer_check_excluded_paths
    ./envoy ./BUILD ./dist)

export ENVOY_BAZEL_PREFIX="@envoy" && ./bazelw run @envoy//tools/code_format:check_format -- "${ENVOY_FORMAT_ACTION}" --path "$PWD" "${FORMAT_ARGS[@]}"

KTFMT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"/ktfmt.sh
KOTLIN_DIRS=(
  "library/kotlin"
  "test/kotlin"
  "examples/kotlin"
)
if [[ "${ENVOY_FORMAT_ACTION}" == "fix" ]]; then
  "${KTFMT}" "${KOTLIN_DIRS[@]}"
else
  NEEDS_FORMAT=$("${KTFMT}" --dry-run "${KOTLIN_DIRS[@]}")
  if [[ -n "${NEEDS_FORMAT}" ]]; then
    echo "ERROR: Run 'tools/check_format.sh fix' to fix"
    echo "${NEEDS_FORMAT}"
    exit 1
  fi
fi
