#!/usr/bin/env bash

set -e

if [[ -s "$1" ]]; then
    cat "$1"
    exit 1
fi
