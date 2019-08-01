ENVOY_BUILD_SHA=$(grep lizan/envoy-build .circleci/config.yml | sed -e 's#.*lizan/envoy-build:\(.*\)#\1#' | uniq)
[[ $(wc -l <<< "${ENVOY_BUILD_SHA}" | awk '{$1=$1};1') == 1 ]] || (echo ".circleci/config.yml hashes are inconsistent!" && exit 1)
