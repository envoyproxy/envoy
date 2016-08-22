#!/bin/bash

for i in `git diff-index --name-only --diff-filter=ACM HEAD 2>&1`; do
  echo "Checking format for $i"
  tools/check_format.py $i clang-format-3.6 check
  if [[ $? -ne 0 ]]; then
    exit 1
  fi
done
