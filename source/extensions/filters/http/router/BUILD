licenses(["notice"])  # Apache 2
# HTTP L7 filter responsible for routing to upstream connection pools
# Public docs: https://github.com/envoyproxy/data-plane-api/blob/master/docs/root/configuration/http_filters/router_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/json:config_schemas_lib",
        "//source/common/router:router_lib",
        "//source/common/router:shadow_writer_lib",
        "@envoy_api//envoy/config/filter/http/router/v2:router_cc",
    ],
)
