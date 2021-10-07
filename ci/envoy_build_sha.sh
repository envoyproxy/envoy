#!/bin/bash

ENVOY_BUILD_SHA=$(grep envoyproxy/envoy-build-centos "$(dirname "$0")"/../.bazelrc | sed -e 's#.*envoyproxy/envoy-build-centos:\(.*\)#\1#' | uniq)
[[ $(wc -l <<< "${ENVOY_BUILD_SHA}" | awk '{$1=$1};1') == 1 ]] || (echo ".bazelrc envoyproxy/envoy-build-centos hashes are inconsistent!" && exit 1)
