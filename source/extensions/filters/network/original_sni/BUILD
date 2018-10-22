licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "original_sni",
    srcs = ["original_sni.cc"],
    hdrs = ["original_sni.h"],
    deps = [
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//source/common/stream_info:original_requested_server_name_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":original_sni",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/network:well_known_names",
    ],
)
