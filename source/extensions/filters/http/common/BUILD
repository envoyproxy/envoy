licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "pass_through_filter_lib",
    hdrs = ["pass_through_filter.h"],
    deps = [
        "//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "empty_http_filter_config_lib",
    hdrs = ["empty_http_filter_config.h"],
    deps = [
        "//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "factory_base_lib",
    hdrs = ["factory_base.h"],
    deps = [
        "//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "jwks_fetcher_lib",
    srcs = ["jwks_fetcher.cc"],
    hdrs = ["jwks_fetcher.h"],
    external_deps = [
        "jwt_verify_lib",
    ],
    deps = [
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/http:utility_lib",
        "@envoy_api//envoy/api/v2/core:http_uri_cc",
    ],
)
