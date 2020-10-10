#!/bin/bash
set -x

source_venv() {
  VENV_DIR=$1
  if [[ "${VIRTUAL_ENV}" == "" ]]; then
    if [[ ! -d "${VENV_DIR}"/venv ]]; then
      virtualenv "${VENV_DIR}"/venv --python=python3.8
    fi
    # shellcheck disable=SC1090
    source "${VENV_DIR}/venv/bin/activate"
  else
    echo "Found existing virtualenv"
  fi
}

python_venv() {
  SCRIPT_DIR=$(realpath "$(dirname "$0")")

  BUILD_DIR=build_tools
  PY_NAME="$1"
  VENV_DIR="${BUILD_DIR}/${PY_NAME}"

  source_venv "${VENV_DIR}"
  which pip # debug, temporary
  pip install -r "${SCRIPT_DIR}"/requirements.txt

  pip list # debug, temporary
  shift
  which python3 # debug, temporary
  python3 "${SCRIPT_DIR}/${PY_NAME}.py" "$*"
}
