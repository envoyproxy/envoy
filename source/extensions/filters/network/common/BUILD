load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "factory_base_lib",
    hdrs = ["factory_base.h"],
    deps = [
        "//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//include/envoy/runtime:runtime_interface",
        "//source/common/common:macros",
        "//source/extensions/common:utility_lib",
        "//source/extensions/filters/network:well_known_names",
    ],
)
