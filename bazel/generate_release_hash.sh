#!/usr/bin/env bash

set -e -o pipefail

ENVOY_SRCDIR="${1}"

if [[ ! -e "$ENVOY_SRCDIR" ]]; then
    echo "Unable to find Envoy src dir: ${ENVOY_SRCDIR}" >&2
    exit 1
fi

git -C "$ENVOY_SRCDIR" fetch --tags

git -C "$ENVOY_SRCDIR" tag --list 'v[0-9]*.[0-9]*.[0-9]*' \
    | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' \
    | sort -u \
    | sha256sum
