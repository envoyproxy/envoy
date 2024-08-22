#!/usr/bin/env bash

CURRENT_SCRIPT_DIR="$(realpath "$(dirname "${BASH_SOURCE[0]}")")"

ENVOY_BUILD_CONTAINER="$(grep envoyproxy/envoy-build-ubuntu "${CURRENT_SCRIPT_DIR}"/../.bazelrc | sed -e 's#.*envoyproxy/envoy-build-ubuntu:\(.*\)#\1#' | uniq)"
ENVOY_BUILD_SHA="$(echo "${ENVOY_BUILD_CONTAINER}" | cut -d@ -f1)"
ENVOY_BUILD_CONTAINER_SHA="$(echo "${ENVOY_BUILD_CONTAINER}" | cut -d@ -f2)"

if [[ -n "$ENVOY_BUILD_CONTAINER_SHA" ]]; then
    ENVOY_BUILD_CONTAINER_SHA="${ENVOY_BUILD_CONTAINER_SHA:7}"
fi

[[ $(wc -l <<< "${ENVOY_BUILD_SHA}" | awk '{$1=$1};1') == 1 ]] || (echo ".bazelrc envoyproxy/envoy-build-ubuntu hashes are inconsistent!" && exit 1)
