ENVOY_BUILD_SHA=$(grep lyft/envoy-build .circleci/config.yml | sed -e 's#.*lyft/envoy-build:\(.*\)#\1#' | head -n 1)
