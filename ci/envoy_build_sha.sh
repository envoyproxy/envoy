if [[ $(uname -m) == "x86_64" ]]; then
	ENVOY_BUILD_SHA=$(grep experimental_docker_image_amd64 $(dirname $0)/../.bazelrc | sed -e 's#.*envoyproxy/envoy-build-ubuntu@sha256:\(.*\)#\1#' | uniq)
elif [[ $(uname -m) == "aarch64" ]]; then
	ENVOY_BUILD_SHA=$(grep experimental_docker_image_arm64 $(dirname $0)/../.bazelrc | sed -e 's#.*envoyproxy/envoy-build-ubuntu@sha256:\(.*\)#\1#' | uniq)
fi
[[ $(wc -l <<< "${ENVOY_BUILD_SHA}" | awk '{$1=$1};1') == 1 ]] || (echo ".bazelrc envoyproxy/envoy-build-ubuntu hashes are inconsistent!" && exit 1)
