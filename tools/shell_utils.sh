#!/bin/bash


source_venv() {
  VENV_DIR=$1
  if [[ "${VIRTUAL_ENV}" == "" ]]; then
    if [[ ! -d "${VENV_DIR}"/venv ]]; then
      virtualenv "${VENV_DIR}"/venv --python=python3
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
  pip3 install -r "${SCRIPT_DIR}"/requirements.txt

  shift
  python3 "${SCRIPT_DIR}/${PY_NAME}.py" "$*"
}
