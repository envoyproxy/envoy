#!/usr/bin/env bash

set -e -o pipefail

LLVM_VERSION=${LLVM_VERSION:-"18.1.8"}
CLANG_VERSION=$(clang --version | grep version | sed -e 's/\ *clang version \([0-9.]*\).*/\1/')
LLVM_COV_VERSION=$(llvm-cov --version | grep version | sed -e 's/\ *LLVM version \([0-9.]*\).*/\1/')
LLVM_PROFDATA_VERSION=$(llvm-profdata show --version | grep version | sed -e 's/\ *LLVM version \(.*\)/\1/')

if [[ -z "$ENVOY_RBE" && "${CLANG_VERSION}" != "${LLVM_VERSION}" ]]; then
    echo "ERROR: clang version ${CLANG_VERSION} does not match expected ${LLVM_VERSION}" >&2
    exit 1
fi

if [[ -z "$ENVOY_RBE" && "${LLVM_COV_VERSION}" != "${LLVM_VERSION}" ]]; then
    echo "ERROR: llvm-cov version ${LLVM_COV_VERSION} does not match expected ${LLVM_VERSION}" >&2
    exit 1
fi

if [[ -z "$ENVOY_RBE" && "${LLVM_PROFDATA_VERSION}" != "${LLVM_VERSION}" ]]; then
    echo "ERROR: llvm-profdata version ${LLVM_PROFDATA_VERSION} does not match expected ${LLVM_VERSION}" >&2
    exit 1
fi

# This is a little hacky
IS_MOBILE="${IS_MOBILE:-}"
if [[ -z "$IS_MOBILE" ]]; then
    cwd="$(basename "$PWD")"
    if [[ "$cwd" == "mobile" ]]; then
        IS_MOBILE=true
    fi
fi

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"
[[ -z "${VALIDATE_COVERAGE}" ]] && VALIDATE_COVERAGE=true
[[ -z "${FUZZ_COVERAGE}" ]] && FUZZ_COVERAGE=false
COVERAGE_TARGET="${COVERAGE_TARGET:-}"
read -ra BAZEL_BUILD_OPTIONS <<< "${BAZEL_BUILD_OPTION_LIST:-}"
read -ra BAZEL_GLOBAL_OPTIONS <<< "${BAZEL_GLOBAL_OPTION_LIST:-}"

# This is the target that will be run to generate coverage data. It can be overridden by consumer
# projects that want to run coverage on a different/combined target.
# Command-line arguments take precedence over ${COVERAGE_TARGET}.
if [[ $# -gt 0 ]]; then
  COVERAGE_TARGETS=("$@")
elif [[ -n "${COVERAGE_TARGET}" ]]; then
  COVERAGE_TARGETS=("${COVERAGE_TARGET}")
else
  COVERAGE_TARGETS=(//test/...)
fi

BAZEL_COVERAGE_OPTIONS=(--heap_dump_on_oom)
BAZEL_VALIDATE_OPTIONS=()

if [[ -n "${BAZEL_GRPC_LOG}" ]]; then
    BAZEL_COVERAGE_OPTIONS+=(--remote_grpc_log="${BAZEL_GRPC_LOG}")
fi

if [[ "${FUZZ_COVERAGE}" == "true" ]]; then
    # Filter targets to just fuzz tests.
    _targets=$(bazel query "${BAZEL_GLOBAL_OPTIONS[@]}" --noshow_loading_progress --noshow_progress "attr('tags', 'fuzz_target', ${COVERAGE_TARGETS[*]})")
    COVERAGE_TARGETS=()
    while read -r line; do COVERAGE_TARGETS+=("$line"); done \
        <<< "$_targets"
    BAZEL_COVERAGE_OPTIONS+=("--config=fuzz-coverage")
    BAZEL_VALIDATE_OPTIONS+=("--config=fuzz-coverage-config")
else
    BAZEL_COVERAGE_OPTIONS+=("--config=test-coverage")
fi

# Output unusually long logs due to trace logging.
BAZEL_COVERAGE_OPTIONS+=("--experimental_ui_max_stdouterr_bytes=80000000")

COVERAGE_DIR="${SRCDIR}/generated/coverage"
if [[ ${FUZZ_COVERAGE} == "true" ]]; then
    COVERAGE_DIR="${SRCDIR}"/generated/fuzz_coverage
fi

run_coverage() {
    echo "Running bazel coverage with:"
    echo "  Options: ${BAZEL_BUILD_OPTIONS[*]} ${BAZEL_COVERAGE_OPTIONS[*]}"
    echo "  Targets: ${COVERAGE_TARGETS[*]}"

    bazel coverage "${BAZEL_BUILD_OPTIONS[@]}" "${BAZEL_COVERAGE_OPTIONS[@]}" "${COVERAGE_TARGETS[@]}"

    if [[ ! -e bazel-out/_coverage/_coverage_report.dat ]]; then
        echo "ERROR: No coverage report found (bazel-out/_coverage/_coverage_report.dat)" >&2
        exit 1
    elif [[ ! -s bazel-out/_coverage/_coverage_report.dat ]]; then
        echo "ERROR: Coverage report is empty (bazel-out/_coverage/_coverage_report.dat)" >&2
        exit 1
    fi
}

unpack_coverage_report() {
    rm -rf "${COVERAGE_DIR}"
    mkdir -p "${COVERAGE_DIR}"
    rm -f bazel-out/_coverage/_coverage_report.tar.zst
    mv bazel-out/_coverage/_coverage_report.dat bazel-out/_coverage/_coverage_report.tar.zst
    bazel run "${BAZEL_BUILD_OPTIONS[@]}" --nobuild_tests_only @zstd//:zstd_cli -- -d -c "${PWD}/bazel-out/_coverage/_coverage_report.tar.zst" \
        | tar -xf - -C "${COVERAGE_DIR}"
    COVERAGE_JSON="${COVERAGE_DIR}/coverage.json"
}

validate_coverage() {
    bazel run \
          "${BAZEL_BUILD_OPTIONS[@]}" \
          "${BAZEL_VALIDATE_OPTIONS[@]}" \
          --nobuild_tests_only \
          @envoy//tools/coverage:validate \
          "$COVERAGE_JSON" \
          "$FUZZ_COVERAGE" \
          "$IS_MOBILE"
}

run_coverage
unpack_coverage_report

if [[ "$VALIDATE_COVERAGE" != "true" ]]; then
    exit 0
fi

validate_coverage
