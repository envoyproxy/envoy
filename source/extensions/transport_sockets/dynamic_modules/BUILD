load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "transport_socket_lib",
    srcs = [
        "abi_impl.cc",
        "stats.cc",
        "transport_socket.cc",
    ],
    hdrs = [
        "config.h",
        "stats.h",
        "transport_socket.h",
        "//source/extensions/dynamic_modules:transport_socket_abi.h",
    ],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl:connection_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/config:datasource_lib",
        "//source/common/config:utility_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/transport_sockets/dynamic_modules/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":transport_socket_lib",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "@envoy_api//envoy/extensions/transport_sockets/dynamic_modules/v3:pkg_cc_proto",
    ],
)
