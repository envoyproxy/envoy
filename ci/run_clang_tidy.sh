#!/bin/bash

set -e

echo "Generating compilation database..."
# The compilation database generate script doesn't support passing build options via CLI.
# Writing them into bazelrc
echo "build ${BAZEL_BUILD_OPTIONS}" >> tools/bazel.rc

# bazel build need to be run to setup virtual includes, generating files which are consumed
# by clang-tidy
"${ENVOY_SRCDIR}/tools/gen_compilation_database.py" --run_bazel_build

if [[ "${RUN_FULL_CLANG_TIDY}" == 1 ]]; then
  echo "Running full clang-tidy..."
  run-clang-tidy-7
elif [[ -z "${CIRCLE_PR_NUMBER}" && "$CIRCLE_BRANCH" == "master" ]]; then
  echo "On master branch, running clang-tidy-diff against previous commit..."
  git diff HEAD^ | clang-tidy-diff-7.py -p 1
else
  echo "Running clang-tidy-diff against master branch..."
  git fetch https://github.com/envoyproxy/envoy.git master
  git diff $(git merge-base HEAD FETCH_HEAD)..HEAD | clang-tidy-diff-7.py -p 1
fi
