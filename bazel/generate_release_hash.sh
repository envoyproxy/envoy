#!/usr/bin/env bash

set -e -o pipefail


git ls-remote --tags https://github.com/envoyproxy/envoy \
    | grep -E 'refs/tags/v[0-9]+\.[0-9]+\.[0-9]+$' \
    | sort -u \
    | sha256sum \
    | cut -d ' ' -f 1
