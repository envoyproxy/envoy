#!/bin/bash

. tools/shell_utils.sh

set -e

SCRIPT_DIR=$(realpath "$(dirname "$0")")
BUILD_DIR=build_tools
VENV_DIR="$BUILD_DIR"/deprecate_version

source_venv "$VENV_DIR"
pip install -r "${SCRIPT_DIR}"/requirements.txt

python "${SCRIPT_DIR}/deprecate_version.py" $*
