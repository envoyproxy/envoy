#!/bin/bash

"$(dirname "$0")"/../git/modified_since_last_github_commit.sh ./ py || \
  [[ "${FORCE_PYTHON_FORMAT}" == "yes" ]] || \
  { echo "Skipping format_python_tools.sh due to no Python changes"; exit 0; }

. tools/shell_utils.sh

FORMAT_ACTION="$1"
FORMAT_PATH="${2:-}"

set -e

# TODO(phlax): move these to bazel
echo "Running Python format check..."
python_venv format_python_tools "$FORMAT_ACTION" "$FORMAT_PATH"

echo "Running Python3 flake8 check..."
python3 -m flake8 --version
# uses .flake8 in repo root
python3 -m flake8 . --count --show-source --statistics
