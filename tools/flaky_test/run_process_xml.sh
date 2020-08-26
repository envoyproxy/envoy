#!/bin/bash

. tools/shell_utils.sh

if [[ "${ENVOY_BUILD_ARCH}" == "aarch64" ]]; then
  MULTIDICT_NO_EXTENSIONS=1
  YARL_NO_EXTENSIONS=1 
  python_venv process_xml $1
else
  python_venv process_xml $1
fi
