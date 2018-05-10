licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

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
