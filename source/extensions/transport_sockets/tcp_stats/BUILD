load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "tcp_stats_lib",
    srcs = ["tcp_stats.cc"],
    hdrs = ["tcp_stats.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:transport_socket_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/transport_sockets/common:passthrough_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tcp_stats/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":tcp_stats_lib",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tcp_stats/v3:pkg_cc_proto",
    ],
)
