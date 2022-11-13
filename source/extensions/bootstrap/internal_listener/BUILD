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
    # for integration tests
    visibility = ["//visibility:public"],
    deps = [
        ":client_connection_factory",
        ":internal_listener_registry",
    ],
)

envoy_cc_extension(
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
        "//source/server:listener_manager_lib",
        "@envoy_api//envoy/extensions/bootstrap/internal_listener/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)

envoy_cc_extension(
    name = "client_connection_factory",
    srcs = [
        "client_connection_factory.cc",
    ],
    hdrs = [
        "client_connection_factory.h",
    ],
    deps = [
        ":thread_local_registry",
        "//envoy/network:client_connection_factory",
        "//envoy/network:connection_interface",
        "//envoy/registry",
        "//source/common/network:connection_lib",
        "//source/common/network:listen_socket_lib",
        "//source/extensions/io_socket/user_space:io_handle_impl_lib",
    ],
)

envoy_cc_library(
    name = "thread_local_registry",
    srcs = [
        "thread_local_registry.cc",
    ],
    hdrs = [
        "client_connection_factory.h",
        "thread_local_registry.h",
    ],
    deps = [
        ":active_internal_listener",
        "//envoy/network:client_connection_factory",
        "//envoy/network:connection_handler_interface",
        "//envoy/thread_local:thread_local_interface",
    ],
)

envoy_cc_library(
    name = "active_internal_listener",
    srcs = ["active_internal_listener.cc"],
    hdrs = [
        "active_internal_listener.h",
    ],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:exception_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:listener_interface",
        "//envoy/stats:timespan_interface",
        "//source/common/common:linked_object",
        "//source/common/common:non_copyable",
        "//source/common/event:deferred_task",
        "//source/common/network:connection_lib",
        "//source/common/network:listener_filter_buffer_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/io_socket/user_space:config",
        "//source/server:active_stream_listener_base",
        "//source/server:active_tcp_listener",
    ],
)
