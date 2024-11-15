load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "factory_base_lib",
    hdrs = ["factory_base.h"],
    # This resource monitoring library is considered core code.
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/server:resource_monitor_config_interface",
        "//source/common/protobuf:utility_lib",
    ],
)
