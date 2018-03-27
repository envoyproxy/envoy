licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "echo",
    srcs = ["echo.cc"],
    hdrs = ["echo.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        ":echo",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
    ],
)
