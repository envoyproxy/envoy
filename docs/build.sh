#!/bin/bash

SCRIPT_DIR=$(dirname "$0")
BUILD_DIR=build/docs
[[ -z "${DOCS_OUTPUT_DIR}" ]] && DOCS_OUTPUT_DIR=generated/docs

rm -rf "${DOCS_OUTPUT_DIR}"
mkdir -p "${DOCS_OUTPUT_DIR}"

if [ ! -d "${BUILD_DIR}"/venv ]; then
  virtualenv "${BUILD_DIR}"/venv --no-site-packages
  "${BUILD_DIR}"/venv/bin/pip install -r "${SCRIPT_DIR}"/requirements.txt
fi

source "${BUILD_DIR}"/venv/bin/activate
cp -r "${SCRIPT_DIR}"/landing_generated/* "${DOCS_OUTPUT_DIR}"
sphinx-build -W -b html "${SCRIPT_DIR}" "${DOCS_OUTPUT_DIR}"/docs
