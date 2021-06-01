load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "filter_config_provider_interface",
    hdrs = ["filter_config_provider.h"],
    deps = [
        "//include/envoy/config:extension_config_provider_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/init:manager_interface",
        "//include/envoy/server:filter_config_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
