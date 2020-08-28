#!/bin/bash

. "${ENVOY_SRCDIR}"/tools/shell_utils.sh

if [[ "${ENVOY_BUILD_ARCH}" == "aarch64" ]]; then
  export MULTIDICT_NO_EXTENSIONS=1
  export YARL_NO_EXTENSIONS=1 
fi

python_venv process_xml $1
