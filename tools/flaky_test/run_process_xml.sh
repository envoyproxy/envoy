#!/bin/bash

. tools/shell_utils.sh

if [[ "${ENVOY_BUILD_ARCH}" == "aarch64" ]]; then
  python_venv_arm process_xml $1
else
  python_venv process_xml $1
fi
