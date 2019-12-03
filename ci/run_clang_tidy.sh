#!/bin/bash

set -eo pipefail

ENVOY_SRCDIR=${ENVOY_SRCDIR:-$(cd $(dirname $0)/.. && pwd)}

export LLVM_CONFIG=${LLVM_CONFIG:-llvm-config}
LLVM_PREFIX=${LLVM_PREFIX:-$(${LLVM_CONFIG} --prefix)}
CLANG_TIDY=${CLANG_TIDY:-$(${LLVM_CONFIG} --bindir)/clang-tidy}
CLANG_APPLY_REPLACEMENTS=${CLANG_APPLY_REPLACEMENTS:-$(${LLVM_CONFIG} --bindir)/clang-apply-replacements}

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
# TODO(scw00): We should run clang-tidy against win32 impl. But currently we only have 
# linux ci box.
function exclude_win32_impl() {
  grep -v source/common/filesystem/win32/ | grep -v source/common/common/win32 | grep -v source/exe/win32
}

# Do not run incremental clang-tidy on check_format testdata files.
function exclude_testdata() {
  grep -v tools/testdata/check_format/
}

# Do not run clang-tidy against Chromium URL import, this needs to largely
# reflect the upstream structure.
function exclude_chromium_url() {
  grep -v source/common/chromium_url/
}

function filter_excludes() {
  exclude_testdata | exclude_chromium_url | exclude_win32_impl
}


if [[ "${RUN_FULL_CLANG_TIDY}" == 1 ]]; then
  echo "Running full clang-tidy..."
  "${LLVM_PREFIX}/share/clang/run-clang-tidy.py" \
    -clang-tidy-binary=${CLANG_TIDY} \
    -clang-apply-replacements-binary=${CLANG_APPLY_REPLACEMENTS} \
    ${APPLY_CLANG_TIDY_FIXES:+-fix}
elif [[ "${BUILD_REASON}" != "PullRequest" ]]; then
  echo "Running clang-tidy-diff against previous commit..."
  git diff HEAD^ | filter_excludes | \
    "${LLVM_PREFIX}/share/clang/clang-tidy-diff.py" \
      -clang-tidy-binary=${CLANG_TIDY} \
      -p 1
else
  echo "Running clang-tidy-diff against master branch..."
  git diff "remotes/origin/${SYSTEM_PULLREQUEST_TARGETBRANCH}" | filter_excludes | \
    "${LLVM_PREFIX}/share/clang/clang-tidy-diff.py" \
      -clang-tidy-binary=${CLANG_TIDY} \
      -p 1
fi
