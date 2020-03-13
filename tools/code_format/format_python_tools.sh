#!/bin/bash

. tools/shell_utils.sh

set -e

echo "Running Python format check..."
python_venv format_python_tools $1

echo "Running Python3 flake8 check..."
python3 -m flake8 --version
python3 -m flake8 . --exclude=*/venv/* --count --select=E9,F63,F72,F82 --show-source --statistics
