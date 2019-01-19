licenses(["notice"])  # Apache 2

# tap wrapper around a transport socket.

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "tap_lib",
    srcs = ["tap.cc"],
    hdrs = ["tap.h"],
    deps = [
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/transport_socket/tap/v2alpha:tap_cc",
        "@envoy_api//envoy/data/tap/v2alpha:transport_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":tap_lib",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/registry",
        "//include/envoy/server:transport_socket_config_interface",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/transport_sockets:well_known_names",
        "@envoy_api//envoy/config/transport_socket/tap/v2alpha:tap_cc",
    ],
)
