load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "reverse_tunnel_acceptor_includes",
    hdrs = [
        "reverse_connection_io_handle.h",
        "reverse_tunnel_acceptor.h",
        "reverse_tunnel_acceptor_extension.h",
        "reverse_tunnel_lifecycle_info.h",
        "reverse_tunnel_upstream_lifecycle.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/access_log:access_log_interface",
        "//envoy/common:time_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/extensions/bootstrap/reverse_tunnel:reverse_tunnel_reporter_lib",
        "//envoy/network:io_handle_interface",
        "//envoy/network:socket_interface",
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stream_info:stream_info_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/config:utility_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:reverse_connection_utility_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:rping_interceptor_lib",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "reverse_tunnel_acceptor_lib",
    srcs = [
        "reverse_connection_io_handle.cc",
        "reverse_tunnel_acceptor.cc",
        "reverse_tunnel_acceptor_extension.cc",
        "reverse_tunnel_upstream_lifecycle.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":reverse_tunnel_acceptor_includes",
        ":upstream_socket_manager_lib",
        "//source/common/access_log:access_log_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/formatter:stream_info_formatter_extension_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/network:socket_lib",
        "//source/common/protobuf",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/stream_info:uint64_accessor_lib",
        "//source/server:generic_factory_context_lib",
    ],
    alwayslink = 1,
)

envoy_cc_extension(
    name = "upstream_socket_manager_lib",
    srcs = ["upstream_socket_manager.cc"],
    hdrs = ["upstream_socket_manager.h"],
    visibility = ["//visibility:public"],
    deps = [
        "reverse_tunnel_acceptor_includes",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:io_handle_interface",
        "//envoy/thread_local:thread_local_object",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:random_generator_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:reverse_connection_utility_lib",
    ],
)
