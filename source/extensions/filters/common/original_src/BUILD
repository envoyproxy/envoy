licenses(["notice"])  # Apache 2

# Helprs for filters for mirroring the downstream remote address on the upstream's source.

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
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "socket_option_factory_lib",
    srcs = ["socket_option_factory.cc"],
    hdrs = ["socket_option_factory.h"],
    deps = [
        ":original_src_socket_option_lib",
        "//include/envoy/network:listen_socket_interface",
        "//source/common/network:address_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:utility_lib",
    ],
)
