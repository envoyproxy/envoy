licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "echo_lib",
    srcs = ["echo.cc"],
    hdrs = ["echo.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/config:well_known_names",
    ],
)
