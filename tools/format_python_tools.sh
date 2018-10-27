#!/bin/bash

set -e

SCRIPTPATH=$( cd "$(dirname "$0")" ; pwd -P )
cd "$SCRIPTPATH"

if [ "${CIRCLECI}" != "true" ]; then
    if [[ "$(which pip)" == "" ]]; then
        echo "Could not install yapf dependecy"
        echo "ERROR: pip not found"
        exit 1
    fi

    echo "Installing requirements..."
    pip install -r requirements.txt
fi

echo "Running Python format check..."
python format_python_tools.py $1
