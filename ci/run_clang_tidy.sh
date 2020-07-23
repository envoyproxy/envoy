#!/bin/bash

set -eo pipefail

ENVOY_SRCDIR=${ENVOY_SRCDIR:-$(cd $(dirname $0)/.. && pwd)}

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

cp -f .bazelrc .bazelrc.bak

function cleanup() {
  cp -f .bazelrc.bak .bazelrc
  rm -f .bazelrc.bak
}
trap cleanup EXIT

# bazel build need to be run to setup virtual includes, generating files which are consumed
# by clang-tidy
"${ENVOY_SRCDIR}/tools/gen_compilation_database.py" --run_bazel_build --include_headers

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

# Exclude files in third_party which are temporary forks from other OSS projects.
function exclude_third_party() {
  grep -v third_party/
}

function filter_excludes() {
  exclude_testdata | exclude_win32_impl | exclude_macos_impl | exclude_third_party
}

if [[ -z "${DIFF_REF}" && "${BUILD_REASON}" != "PullRequest" ]]; then
  DIFF_REF=HEAD^
fi

if [[ "${RUN_FULL_CLANG_TIDY}" == 1 ]]; then
  echo "Running full clang-tidy..."
  python3 "${LLVM_PREFIX}/share/clang/run-clang-tidy.py" \
    -clang-tidy-binary=${CLANG_TIDY} \
    -clang-apply-replacements-binary=${CLANG_APPLY_REPLACEMENTS} \
    -export-fixes=${FIX_YAML} \
    -j ${NUM_CPUS:-0} -p 1 -quiet \
    ${APPLY_CLANG_TIDY_FIXES:+-fix}
elif [[ -n "${DIFF_REF}" ]]; then
  echo "Running clang-tidy-diff against ref ${DIFF_REF}"
  git diff ${DIFF_REF} | filter_excludes | \
    python3 "${LLVM_PREFIX}/share/clang/clang-tidy-diff.py" \
      -clang-tidy-binary=${CLANG_TIDY} \
      -export-fixes=${FIX_YAML} \
      -j ${NUM_CPUS:-0} -p 1 -quiet
else
  echo "Running clang-tidy-diff against master branch..."
  git diff "remotes/origin/${SYSTEM_PULLREQUEST_TARGETBRANCH}" | filter_excludes | \
    python3 "${LLVM_PREFIX}/share/clang/clang-tidy-diff.py" \
      -clang-tidy-binary=${CLANG_TIDY} \
      -export-fixes=${FIX_YAML} \
      -j ${NUM_CPUS:-0} -p 1 -quiet
fi

if [[ -s "${FIX_YAML}" ]]; then
  echo "clang-tidy check failed, potentially fixed by clang-apply-replacements:"
  cat ${FIX_YAML}
  exit 1
fi
