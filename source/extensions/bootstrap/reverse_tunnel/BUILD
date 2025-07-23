load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
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

envoy_cc_extension(
    name = "reverse_tunnel_initiator_lib",
    srcs = [
        "grpc_reverse_tunnel_client.cc",
        "reverse_tunnel_initiator.cc",
    ],
    hdrs = [
        "factory_base.h",
        "grpc_reverse_tunnel_client.h",
        "reverse_tunnel_initiator.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":reverse_connection_address_lib",
        ":reverse_connection_resolver_lib",
        "//envoy/api:io_error_interface",
        "//envoy/grpc:async_client_interface",
        "//envoy/network:address_interface",
        "//envoy/network:io_handle_interface",
        "//envoy/network:socket_interface",
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/tracing:trace_driver_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/http:headers_lib",
        "//source/common/network:address_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/network:filter_lib",
        "//source/common/protobuf",
        "//source/common/reverse_connection:reverse_connection_utility_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/bootstrap/reverse_connection_socket_interface/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/reverse_conn/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/reverse_tunnel/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)

envoy_cc_extension(
    name = "reverse_tunnel_acceptor_lib",
    srcs = ["reverse_tunnel_acceptor.cc"],
    hdrs = [
        "factory_base.h",
        "reverse_tunnel_acceptor.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
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
        "//source/common/reverse_connection:reverse_connection_utility_lib",
        "@envoy_api//envoy/extensions/bootstrap/reverse_connection_socket_interface/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)
