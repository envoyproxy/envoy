licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "original_dst_lib",
    srcs = ["original_dst.cc"],
    hdrs = ["original_dst.h"],
    deps = [
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/network:utility_lib",
    ],
)

envoy_cc_library(
    name = "proxy_protocol_lib",
    srcs = ["proxy_protocol.cc"],
    hdrs = ["proxy_protocol.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
    ],
)
