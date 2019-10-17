#!/bin/bash

set -e

# Quick syntax check of .clang-tidy.
clang-tidy -dump-config > /dev/null 2> clang-tidy-config-errors.txt
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

LLVM_PREFIX=$(llvm-config --prefix)

if [[ "${RUN_FULL_CLANG_TIDY}" == 1 ]]; then
  echo "Running full clang-tidy..."
  "${LLVM_PREFIX}/share/clang/run-clang-tidy.py"
elif [[ -z "${CIRCLE_PR_NUMBER}" && "$CIRCLE_BRANCH" == "master" ]]; then
  echo "On master branch, running clang-tidy-diff against previous commit..."
  git diff HEAD^ | filter_excludes | "${LLVM_PREFIX}/share/clang/clang-tidy-diff.py" -p 1
else
  echo "Running clang-tidy-diff against master branch..."
  git fetch https://github.com/envoyproxy/envoy.git master
  git diff $(git merge-base HEAD FETCH_HEAD)..HEAD | filter_excludes | \
     "${LLVM_PREFIX}/share/clang/clang-tidy-diff.py" -p 1
fi
