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
"${ENVOY_SRCDIR}/tools/gen_compilation_database.py" --include_headers

# Do not run clang-tidy against win32 impl
# TODO(scw00): We should run clang-tidy against win32 impl once we have clang-cl support for Windows
function exclude_win32_impl() {
  grep -v source/common/filesystem/win32/ | grep -v source/common/common/win32 | grep -v source/exe/win32 | grep -v source/common/api/win32
}

# Do not run clang-tidy against macOS impl
# TODO: We should run clang-tidy against macOS impl for completeness
function exclude_macos_impl() {
  grep -v source/common/filesystem/kqueue/
}

# Do not run incremental clang-tidy on check_format testdata files.
function exclude_testdata() {
  grep -v tools/testdata/check_format/
}

# Do not run clang-tidy on envoy_headersplit testdata files.
function exclude_testdata() {
  grep -v tools/envoy_headersplit/
}

# Exclude files in third_party which are temporary forks from other OSS projects.
function exclude_third_party() {
  grep -v third_party/
}

function filter_excludes() {
  exclude_testdata | exclude_win32_impl | exclude_macos_impl | exclude_third_party
}

function run_clang_tidy() {
  python3 "${LLVM_PREFIX}/share/clang/run-clang-tidy.py" \
    -clang-tidy-binary=${CLANG_TIDY} \
    -clang-apply-replacements-binary=${CLANG_APPLY_REPLACEMENTS} \
    -export-fixes=${FIX_YAML} -j ${NUM_CPUS:-0} -p ${SRCDIR} -quiet \
    ${APPLY_CLANG_TIDY_FIXES:+-fix} $@
}

function run_clang_tidy_diff() {
  git diff $1 | filter_excludes | \
    python3 "${LLVM_PREFIX}/share/clang/clang-tidy-diff.py" \
      -clang-tidy-binary=${CLANG_TIDY} \
      -export-fixes=${FIX_YAML} -j ${NUM_CPUS:-0} -p 1 -quiet
}

if [[ $# -gt 0 ]]; then
  echo "Running clang-tidy on: $@"
  run_clang_tidy $@
elif [[ "${RUN_FULL_CLANG_TIDY}" == 1 ]]; then
  echo "Running a full clang-tidy"
  run_clang_tidy
else
  if [[ -z "${DIFF_REF}" ]]; then
    if [[ "${BUILD_REASON}" == "PullRequest" ]]; then
      DIFF_REF="remotes/origin/${SYSTEM_PULLREQUEST_TARGETBRANCH}"
    elif [[ "${BUILD_REASON}" == *CI ]]; then
      DIFF_REF="HEAD^"
    else
      DIFF_REF=$(${ENVOY_SRCDIR}/tools/git/last_github_commit.sh)
    fi
  fi
  echo "Running clang-tidy-diff against ${DIFF_REF} ($(git rev-parse ${DIFF_REF})), current HEAD ($(git rev-parse HEAD))"
  run_clang_tidy_diff ${DIFF_REF}
fi

if [[ -s "${FIX_YAML}" ]]; then
  echo "clang-tidy check failed, potentially fixed by clang-apply-replacements:"
  cat ${FIX_YAML}
  exit 1
fi
