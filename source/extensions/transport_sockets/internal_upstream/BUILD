load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "internal_upstream",
    srcs = ["internal_upstream.cc"],
    hdrs = ["internal_upstream.h"],
    deps = [
        "//source/extensions/io_socket/user_space:io_handle_lib",
        "//source/extensions/transport_sockets/common:passthrough_lib",
        "@envoy_api//envoy/extensions/transport_sockets/internal_upstream/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":internal_upstream",
        "//envoy/common:hashable_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:logger_lib",
        "//source/common/common:scalar_to_byte_vector_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/io_socket/user_space:io_handle_lib",
        "//source/extensions/transport_sockets/common:passthrough_lib",
        "@envoy_api//envoy/extensions/transport_sockets/internal_upstream/v3:pkg_cc_proto",
    ],
)
