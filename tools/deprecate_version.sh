#!/bin/bash

set -e

SCRIPT_DIR=$(realpath "$(dirname "$0")")

mkdir -p build
cd build

# TODO(htuch): avoid creating virtualenv every time
virtualenv deprecate_version
source deprecate_version/bin/activate
pip install gitpython
pip install pygithub

deprecate_version/bin/python "${SCRIPT_DIR}/deprecate_version.py"
