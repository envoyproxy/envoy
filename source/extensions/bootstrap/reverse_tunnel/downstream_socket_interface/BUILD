load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "reverse_connection_address_lib",
    srcs = ["reverse_connection_address.cc"],
    hdrs = ["reverse_connection_address.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/network:address_interface",
        "//source/common/network:address_lib",
        "//source/common/network:socket_interface_lib",
    ],
)

envoy_cc_extension(
    name = "reverse_connection_resolver_lib",
    srcs = ["reverse_connection_resolver.cc"],
    hdrs = ["reverse_connection_resolver.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":reverse_connection_address_lib",
        "//envoy/network:resolver_interface",
        "//envoy/registry",
    ],
)

envoy_cc_library(
    name = "reverse_tunnel_extension_lib",
    srcs = ["reverse_tunnel_initiator_extension.cc"],
    hdrs = ["reverse_tunnel_initiator_extension.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/server:bootstrap_extension_config_interface",
        "//envoy/stats:stats_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:logger_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:reverse_connection_utility_lib",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "reverse_connection_io_handle_lib",
    srcs = [
        "downstream_reverse_connection_io_handle.cc",
        "rc_connection_wrapper.cc",
        "reverse_connection_io_handle.cc",
    ],
    hdrs = [
        "downstream_reverse_connection_io_handle.h",
        "rc_connection_wrapper.h",
        "reverse_connection_io_handle.h",
        "reverse_connection_load_balancer_context.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":reverse_connection_address_lib",
        ":reverse_tunnel_extension_lib",
        "//envoy/api:io_error_interface",
        "//envoy/grpc:async_client_interface",
        "//envoy/network:address_interface",
        "//envoy/network:io_handle_interface",
        "//envoy/network:socket_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/event:real_time_system_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/http:codec_client_lib",
        "//source/common/http:response_decoder_impl_base",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http1:codec_stats_lib",
        "//source/common/network:address_lib",
        "//source/common/network:connection_socket_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/network:filter_lib",
        "//source/common/tls:ssl_handshaker_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:reverse_connection_utility_lib",
    ],
)

envoy_cc_extension(
    name = "reverse_tunnel_initiator_lib",
    srcs = ["reverse_tunnel_initiator.cc"],
    hdrs = ["reverse_tunnel_initiator.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":reverse_connection_address_lib",
        ":reverse_connection_io_handle_lib",
        ":reverse_tunnel_extension_lib",
        "//envoy/network:socket_interface",
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//source/common/common:logger_lib",
        "//source/common/network:address_lib",
        "//source/common/network:socket_interface_lib",
        "//source/common/protobuf:utility_lib",
    ],
)
