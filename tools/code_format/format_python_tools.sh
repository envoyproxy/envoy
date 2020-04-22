#!/bin/bash

"$(dirname "$0")"/../git/modified_since_last_github_commit.sh ./ py || \
  [[ "${FORCE_PYTHON_FORMAT}" == "yes" ]] || \
  { echo "Skipping format_python_tools.sh due to no Python changes"; exit 0; }

. tools/shell_utils.sh

set -e

echo "Running Python format check..."
python_venv format_python_tools $1

echo "Running Python3 flake8 check..."
python3 -m flake8 --version
python3 -m flake8 . --exclude=*/venv/* --count --select=E9,F63,F72,F82 --show-source --statistics
