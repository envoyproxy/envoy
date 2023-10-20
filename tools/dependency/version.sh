#!/bin/bash -e

set -o pipefail

JQ="$1"
VERSIONS="$2"
DEP="$3"

if [[ -z "$DEP" ]]; then
    echo "You must specify what to check version for" >&2
    exit 1
fi

$JQ -r ".${DEP}" "$VERSIONS"
