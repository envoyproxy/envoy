load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "reverse_connection_listener_config_lib",
    srcs = ["reverse_connection_listener_config_impl.cc"],
    hdrs = ["reverse_connection_listener_config_impl.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/network:listener_interface",
        "//source/extensions/bootstrap/reverse_connection:reverse_conn_global_registry",
    ],
)

envoy_cc_library(
    name = "active_reverse_connection_listener_lib",
    srcs = ["active_reverse_connection_listener.cc"],
    hdrs = [
        "active_reverse_connection_listener.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listener_interface",
        "//source/common/listener_manager:active_stream_listener_base",
        "//source/server:active_listener_base",
    ],
)
