#!/usr/bin/env bash

# Compatibility shim: coverage tooling now lives in @envoy_toolshed//coverage.
# Translates the legacy env-var interface to the toolshed driver's flags.

set -e -o pipefail

read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTION_LIST:-}"

IS_MOBILE="${IS_MOBILE:-}"
if [[ -z "$IS_MOBILE" && "$(basename "$PWD")" == "mobile" ]]; then
    IS_MOBILE=true
fi

if [[ "$IS_MOBILE" == "true" ]]; then
    VALIDATE_TARGET=@envoy_mobile//test:coverage_report.validate
else
    VALIDATE_TARGET=@envoy//tools/coverage:report.validate
fi

ARGS=(--validate-target="$VALIDATE_TARGET")

if [[ "${FUZZ_COVERAGE:-false}" == "true" ]]; then
    ARGS+=(--fuzz --config=fuzz-coverage --validate-config=fuzz-coverage-config)
else
    ARGS+=(--config=test-coverage)
fi
if [[ "$IS_MOBILE" == "true" ]]; then
    ARGS+=(--summary-only)
fi
if [[ "${VALIDATE_COVERAGE:-true}" != "true" ]]; then
    ARGS+=(--no-validate)
fi
if [[ -n "${BAZEL_GRPC_LOG:-}" ]]; then
    ARGS+=(--grpc-log="$BAZEL_GRPC_LOG")
fi

exec bazel run "${BAZEL_BUILD_OPTIONS[@]}" --nobuild_tests_only \
    @envoy_toolshed//coverage:run -- "${ARGS[@]}" -- "$@"
