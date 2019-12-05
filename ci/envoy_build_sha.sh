ENVOY_BUILD_SHA=$(grep envoyproxy/envoy-build-ubuntu@sha256 $(dirname $0)/../.bazelrc | sed -e 's#.*envoyproxy/envoy-build-ubuntu@sha256:\(.*\)#\1#' | uniq)
[[ $(wc -l <<< "${ENVOY_BUILD_SHA}" | awk '{$1=$1};1') == 1 ]] || (echo ".bazelrc envoyproxy/envoy-build-ubuntu hashes are inconsistent!" && exit 1)
