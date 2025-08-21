load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "config_provider_manager_interface",
    hdrs = ["config_provider_manager.h"],
    deps = [
        "//envoy/config:dynamic_extension_config_provider_interface",
        "//envoy/init:manager_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/upstream:cluster_manager_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
