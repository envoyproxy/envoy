licenses(["notice"])  # Apache 2

# A filter for mirroring the downstream remote address on the upstream's source.

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "original_src_socket_option_lib",
    srcs = ["original_src_socket_option.cc"],
    hdrs = ["original_src_socket_option.h"],
    deps = [
        "//include/envoy/network:listen_socket_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/network:address_lib",
    ],
)

envoy_cc_library(
    name = "original_src",
    srcs = ["original_src.cc"],
    hdrs = ["original_src.h"],
    deps = [
        ":original_src_socket_option_lib",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        ":original_src",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/network:well_known_names",
    ],
)
