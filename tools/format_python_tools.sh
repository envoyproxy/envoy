#!/bin/bash

set -e

VENV_DIR="pyformat"
SCRIPTPATH=$(realpath "$(dirname $0)")
cd "$SCRIPTPATH"

if [ "${CIRCLECI}" != "true" ]; then

  if [[ "$VIRTUAL_ENV" == "" ]]; then
    if [[ ! -d "${VENV_DIR}"/venv ]]; then
      virtualenv "${VENV_DIR}"/venv --no-site-packages
    fi
    source "${VENV_DIR}"/venv/bin/activate
  else
    echo "Found existing virtualenv"
  fi

  echo "Installing requirements..."
  pip install -r requirements.txt
fi

echo "Running Python format check..."
python format_python_tools.py $1
