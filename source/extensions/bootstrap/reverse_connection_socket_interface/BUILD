load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "reverse_connection_utility_lib",
    srcs = ["reverse_connection_utility.cc"],
    hdrs = ["reverse_connection_utility.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
    ],
)

envoy_cc_extension(
    name = "upstream_reverse_connection_socket_interface_lib",
    srcs = [
        "reverse_tunnel_acceptor.cc",
    ],
    hdrs = [
        "reverse_tunnel_acceptor.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":reverse_connection_utility_lib",
        "//envoy/common:random_generator_interface",
        "//envoy/network:address_interface",
        "//envoy/network:io_handle_interface",
        "//envoy/network:socket_interface",
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/thread_local:thread_local_object",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:random_generator_lib",
        "//source/common/network:address_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/protobuf",
    ],
    alwayslink = 1,
)
