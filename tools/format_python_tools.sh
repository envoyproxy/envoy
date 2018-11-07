#!/bin/bash

. tools/shell_utils.sh

set -e

VENV_DIR="pyformat"
SCRIPTPATH=$(realpath "$(dirname $0)")
cd "$SCRIPTPATH"

if [ "${CIRCLECI}" != "true" ]; then
  source_venv "$VENV_DIR"
  echo "Installing requirements..."
  pip install -r requirements.txt
fi

echo "Running Python format check..."
python format_python_tools.py $1
