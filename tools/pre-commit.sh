#!/bin/bash

SCRIPT_DIR="$(dirname "$(realpath "$0")")"

for i in `git diff-index --name-only --diff-filter=ACM HEAD 2>&1`; do
  echo "Checking format for $i"
  "$SCRIPT_DIR"/check_format.py check $i
  if [[ $? -ne 0 ]]; then
    exit 1
  fi
done
