#!/bin/bash

set -eo pipefail

# ENVOY_SRCDIR should point to where Envoy source lives, while SRCDIR could be a downstream build
# (for example envoy-filter-example).
[[ -z "${ENVOY_SRCDIR}" ]] && ENVOY_SRCDIR="${PWD}"
[[ -z "${SRCDIR}" ]] && SRCDIR="${ENVOY_SRCDIR}"

export LLVM_CONFIG=${LLVM_CONFIG:-llvm-config}
LLVM_PREFIX=${LLVM_PREFIX:-$(${LLVM_CONFIG} --prefix)}
CLANG_TIDY=${CLANG_TIDY:-$(${LLVM_CONFIG} --bindir)/clang-tidy}
CLANG_APPLY_REPLACEMENTS=${CLANG_APPLY_REPLACEMENTS:-$(${LLVM_CONFIG} --bindir)/clang-apply-replacements}
FIX_YAML=clang-tidy-fixes.yaml

# Quick syntax check of .clang-tidy.
${CLANG_TIDY} -dump-config > /dev/null 2> clang-tidy-config-errors.txt
if [[ -s clang-tidy-config-errors.txt ]]; then
  cat clang-tidy-config-errors.txt
  rm clang-tidy-config-errors.txt
  exit 1
fi
rm clang-tidy-config-errors.txt

echo "Generating compilation database..."

# bazel build need to be run to setup virtual includes, generating files which are consumed
# by clang-tidy
read -ra COMP_DB_TARGETS <<< "$COMP_DB_TARGETS"
"${ENVOY_SRCDIR}/tools/gen_compilation_database.py" --include_headers "${COMP_DB_TARGETS[@]}"

# Do not run clang-tidy against win32 impl
# TODO(scw00): We should run clang-tidy against win32 impl once we have clang-cl support for Windows
function exclude_win32_impl() {
  grep -v source/common/filesystem/win32/ | grep -v source/common/common/win32 | grep -v source/exe/win32 | grep -v source/common/api/win32 | grep -v source/common/event/win32
}

# Do not run clang-tidy against macOS impl
# TODO: We should run clang-tidy against macOS impl for completeness
function exclude_macos_impl() {
  grep -v source/common/filesystem/kqueue/ | grep -v source/extensions/network/dns_resolver/apple/apple_dns_impl | grep -v test/extensions/network/dns_resolver/apple/apple_dns_impl_test
}

# Do not run incremental clang-tidy on check_format testdata files.
function exclude_check_format_testdata() {
  grep -v tools/testdata/check_format/
}

# Exclude files in third_party which are temporary forks from other OSS projects.
function exclude_third_party() {
  grep -v third_party/ | grep -v bazel/external/http_parser
}

# Exclude files which are part of the Wasm emscripten environment
function exclude_wasm_emscripten() {
  grep -v source/extensions/common/wasm/ext
}

# Exclude files which are part of the Wasm SDK
function exclude_wasm_sdk() {
  grep -v proxy_wasm_cpp_sdk
}

# Exclude files which are part of the Wasm Host environment
function exclude_wasm_host() {
  grep -v proxy_wasm_cpp_host
}

# Exclude proxy-wasm test_data.
function exclude_wasm_test_data() {
  grep -v wasm/test_data
}

# Exclude files which are part of the Wasm examples
function exclude_wasm_examples() {
  grep -v examples/wasm
}

# Exclude envoy mobile.
function exclude_envoy_mobile() {
  grep -v mobile/library | grep -v mobile/test
}

function filter_excludes() {
  exclude_envoy_mobile | exclude_check_format_testdata | exclude_win32_impl | exclude_macos_impl | exclude_third_party | exclude_wasm_emscripten | exclude_wasm_sdk | exclude_wasm_host | exclude_wasm_test_data | exclude_wasm_examples
}

function run_clang_tidy() {
  python3 "${LLVM_PREFIX}/share/clang/run-clang-tidy.py" \
    -clang-tidy-binary="${CLANG_TIDY}" \
    -clang-apply-replacements-binary="${CLANG_APPLY_REPLACEMENTS}" \
    -export-fixes=${FIX_YAML} -j "${NUM_CPUS:-0}" -p "${SRCDIR}" -quiet \
    ${APPLY_CLANG_TIDY_FIXES:+-fix} "$@"
}

function run_clang_tidy_diff() {
  local diff
  diff="$(git diff "${1}")"
  if [[ -z "$diff" ]]; then
    echo "No changes detected, skipping clang_tidy_diff"
    return 0
  fi
  echo "$diff" | filter_excludes | \
    python3 "${LLVM_PREFIX}/share/clang/clang-tidy-diff.py" \
      -clang-tidy-binary="${CLANG_TIDY}" \
      -export-fixes="${FIX_YAML}" -j "${NUM_CPUS:-0}" -p 1 -quiet
}

if [[ $# -gt 0 ]]; then
  echo "Running clang-tidy on: $*"
  run_clang_tidy "$@"
elif [[ "${RUN_FULL_CLANG_TIDY}" == 1 ]]; then
  echo "Running a full clang-tidy"
  run_clang_tidy
else
  if [[ -z "${DIFF_REF}" ]]; then
    if [[ "${BUILD_REASON}" == *CI ]]; then
      DIFF_REF="HEAD^"
    else
      DIFF_REF=$("${ENVOY_SRCDIR}"/tools/git/last_github_commit.sh)
    fi
  fi
  echo "Running clang-tidy-diff against ${DIFF_REF} ($(git rev-parse "${DIFF_REF}")), current HEAD ($(git rev-parse HEAD))"
  run_clang_tidy_diff "${DIFF_REF}"
fi

if [[ -s "${FIX_YAML}" ]]; then
  echo "clang-tidy check failed, potentially fixed by clang-apply-replacements:"
  cat "${FIX_YAML}"
  exit 1
fi
