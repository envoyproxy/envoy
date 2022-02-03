load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.h"],
    # for integration tests
    visibility = ["//visibility:public"],
    deps = [
        ":client_connection_factory",
        ":internal_listener_registry",
        ":io_handle_impl_lib",
    ],
)

envoy_cc_library(
    name = "io_handle_lib",
    hdrs = ["io_handle.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:empty_string",
    ],
)

envoy_cc_library(
    name = "file_event_lib",
    srcs = [
        "file_event_impl.cc",
    ],
    hdrs = [
        "file_event_impl.h",
    ],
    deps = [
        ":io_handle_lib",
        "//envoy/event:dispatcher_interface",
    ],
)

envoy_cc_library(
    name = "io_handle_impl_lib",
    srcs = [
        "io_handle_impl.cc",
    ],
    hdrs = [
        "io_handle_impl.h",
    ],
    deps = [
        ":file_event_lib",
        ":io_handle_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/network:address_lib",
        "//source/common/network:default_socket_interface_lib",
    ],
)

envoy_cc_library(
    name = "client_connection_factory",
    srcs = [
        "client_connection_factory.cc",
    ],
    hdrs = [
        "client_connection_factory.h",
    ],
    deps = [
        ":io_handle_impl_lib",
        ":thread_local_registry",
        "//envoy/network:client_connection_factory",
        "//envoy/network:connection_interface",
        "//envoy/registry",
        "//source/common/network:connection_lib",
        "//source/common/network:listen_socket_lib",
    ],
)

envoy_cc_library(
    name = "internal_listener_registry",
    srcs = [
        "internal_listener_registry.cc",
    ],
    hdrs = [
        "internal_listener_registry.h",
    ],
    deps = [
        ":thread_local_registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "@envoy_api//envoy/extensions/io_socket/user_space/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "thread_local_registry",
    hdrs = [
        "client_connection_factory.h",
        "thread_local_registry.h",
    ],
    deps = [
        "//envoy/network:client_connection_factory",
        "//envoy/network:listener_interface",
        "//envoy/thread_local:thread_local_interface",
    ],
)
