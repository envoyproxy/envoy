ENVOY_BUILD_TAG=$(grep envoyproxy/envoy-build-ubuntu $(dirname $0)/../.bazelrc | sed -e 's#.*envoyproxy/envoy-build-ubuntu:\(.*\)#\1#' | uniq)
[[ $(wc -l <<< "${ENVOY_BUILD_TAG}" | awk '{$1=$1};1') == 1 ]] || (echo ".bazelrc envoyproxy/envoy-build-ubuntu hashes are inconsistent!" && exit 1)
