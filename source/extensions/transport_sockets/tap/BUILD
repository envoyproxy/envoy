load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_select_admin_functionality",
)

licenses(["notice"])  # Apache 2

# tap wrapper around a transport socket.

envoy_extension_package()

envoy_cc_library(
    name = "tap_config_interface",
    hdrs = ["tap_config.h"],
    deps = [
        "//envoy/network:connection_interface",
        "//source/extensions/common/tap:tap_interface",
    ],
)

envoy_cc_library(
    name = "tap_config_impl",
    srcs = ["tap_config_impl.cc"],
    hdrs = ["tap_config_impl.h"],
    deps = [
        ":tap_config_interface",
        "//source/common/network:utility_lib",
        "//source/extensions/common/tap:tap_config_base",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "tap_lib",
    srcs = envoy_select_admin_functionality(["tap.cc"]),
    hdrs = envoy_select_admin_functionality(["tap.h"]),
    deps = [
        ":tap_config_interface",
        "//envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/common/tap:extension_config_base",
        "//source/extensions/transport_sockets/common:passthrough_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = envoy_select_admin_functionality(["config.cc"]),
    hdrs = envoy_select_admin_functionality(["config.h"]),
    deps = [
        ":tap_config_impl",
        ":tap_lib",
        "//envoy/network:transport_socket_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tap/v3:pkg_cc_proto",
    ],
)
