#!/bin/bash

set -e

SCRIPT_DIR=$(realpath "$(dirname "$0")")
BUILD_DIR=build
VENV_DIR=build/deprecate_version

if [ ! -d "${VENV_DIR}" ]; then
  virtualenv "${VENV_DIR}" --no-site-packages --python=python2.7
  "${VENV_DIR}"/bin/pip install -r "${SCRIPT_DIR}"/requirements.txt
fi

source "${VENV_DIR}"/bin/activate
"${VENV_DIR}"/bin/python "${SCRIPT_DIR}/deprecate_version.py" $*
