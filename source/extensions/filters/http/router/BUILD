licenses(["notice"])  # Apache 2

# HTTP L7 filter responsible for routing to upstream connection pools
# Public docs: docs/root/configuration/http_filters/router_filter.rst

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
        "//source/common/config:filter_json_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/router:router_lib",
        "//source/common/router:shadow_writer_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
    ],
)
