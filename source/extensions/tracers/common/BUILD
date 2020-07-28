load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_extension_library(
    name = "factory_base_lib",
    hdrs = ["factory_base.h"],
    deps = [
        "//include/envoy/server:tracer_config_interface",
        "//source/common/config:utility_lib",
    ],
)
