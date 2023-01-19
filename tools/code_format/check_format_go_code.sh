#!/bin/bash

set -e

tools="$(dirname "$(dirname "$(realpath "$0")")")"
root=$(realpath "$tools/..")

cd "$root" || exit 1

gofmt=bazel-bin/tools/code_format/gofmt

# only get the filenames which not satisfy gofmt
files=$(find . -name "*.go" -exec $gofmt -l {} \;)
if [[ $files != "" ]]; then
  # write changes to original files, so that we can get the changes by git diff.
  find . -name "*.go" -exec $gofmt -w {} \;
  echo "ERROR: files not satisfy gofmt:"
  echo "$files"
  exit 1
fi
