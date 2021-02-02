#!/bin/bash

. tools/shell_utils.sh

set -e

python_venv release_dates "$1"
