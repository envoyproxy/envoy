#!/bin/bash

set -e

SCRIPT_DIR=$(realpath "$(dirname "$0")")
BUILD_DIR=build
VENV_DIR=build/deprecate_version

if [[ "$VIRTUAL_ENV" == "" ]]; then
  if [ ! -d "${VENV_DIR}" ]; then
    virtualenv "${VENV_DIR}" --no-site-packages --python=python2.7
  fi
  source "${VENV_DIR}"/bin/activate
else
  echo "Found existing virtualenv"
fi

pip install -r "${SCRIPT_DIR}"/requirements.txt

python "${SCRIPT_DIR}/deprecate_version.py" $*
