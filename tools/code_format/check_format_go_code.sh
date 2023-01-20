#!/bin/bash

set -e

GOFMT_BIN="bazel run @go_sdk//:bin/gofmt -- "

tools="$(dirname "$(dirname "$(realpath "$0")")")"
root=$(realpath "$tools/..")

cd "$root" || exit 1

# all go files
go_files=$(find . -name "*.go")

# only get the filenames which not satisfy gofmt
files=$($GOFMT_BIN -l ${go_files[@]})
if [[ $files != "" ]]; then
  # write changes to original files, so that we can get the changes by git diff.
  $GOFMT_BIN -w ${go_files[@]}
  echo "ERROR: files not satisfy gofmt:"
  echo "$files"
  exit 1
fi
