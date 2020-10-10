#!/bin/bash

# shellcheck source=tools/shell_utils.sh
. "${ENVOY_SRCDIR}"/tools/shell_utils.sh

python_venv process_xml "$1"
