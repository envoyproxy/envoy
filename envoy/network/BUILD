load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "address_interface",
    hdrs = ["address.h"],
)

envoy_cc_library(
    name = "connection_interface",
    hdrs = ["connection.h"],
    external_deps = ["abseil_int128"],
    deps = [
        ":address_interface",
        ":filter_interface",
        ":listen_socket_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/ssl:connection_interface",
        "//envoy/stream_info:stream_info_interface",
    ],
)

envoy_cc_library(
    name = "client_connection_factory",
    hdrs = ["client_connection_factory.h"],
    deps = [
        ":address_interface",
        ":connection_interface",
        ":listen_socket_interface",
        ":transport_socket_interface",
        "//envoy/config:typed_config_interface",
    ],
)

envoy_cc_library(
    name = "connection_handler_interface",
    hdrs = ["connection_handler.h"],
    deps = [
        ":connection_balancer_interface",
        ":listen_socket_interface",
        ":listener_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/ssl:context_interface",
        "//source/common/common:interval_value",
    ],
)

envoy_cc_library(
    name = "udp_packet_writer_handler_interface",
    hdrs = ["udp_packet_writer_handler.h"],
    deps = [
        ":address_interface",
        ":io_handle_interface",
        ":socket_interface",
        "//envoy/api:io_error_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/config:typed_metadata_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "dns_interface",
    hdrs = ["dns.h"],
    deps = ["//envoy/network:address_interface"],
)

envoy_cc_library(
    name = "dns_resolver_interface",
    hdrs = ["dns_resolver.h"],
    deps = [
        "//envoy/api:api_interface",
        "//source/common/config:utility_lib",
    ],
)

envoy_cc_library(
    name = "drain_decision_interface",
    hdrs = ["drain_decision.h"],
    external_deps = ["abseil_base"],
    deps = ["//envoy/common:callback"],
)

envoy_cc_library(
    name = "exception_interface",
    hdrs = ["exception.h"],
)

envoy_cc_library(
    name = "filter_interface",
    hdrs = ["filter.h"],
    deps = [
        ":listen_socket_interface",
        ":listener_filter_buffer_interface",
        ":transport_socket_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/stream_info:stream_info_interface",
        "//envoy/upstream:host_description_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "hash_policy_interface",
    hdrs = ["hash_policy.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":connection_interface",
    ],
)

envoy_cc_library(
    name = "io_handle_interface",
    hdrs = ["io_handle.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":address_interface",
        "//envoy/api:io_error_interface",
        "//envoy/api:os_sys_calls_interface",
        "//envoy/event:file_event_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "socket_interface",
    hdrs = ["socket.h"],
    deps = [
        ":address_interface",
        ":io_handle_interface",
        "//envoy/ssl:connection_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "socket_interface_interface",
    hdrs = ["socket_interface.h"],
    deps = [
        ":socket_interface",
        "//envoy/config:typed_config_interface",
    ],
)

envoy_cc_library(
    name = "listen_socket_interface",
    hdrs = ["listen_socket.h"],
    deps = [
        ":io_handle_interface",
        ":socket_interface",
        "//envoy/common:scope_tracker_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "listener_filter_buffer_interface",
    hdrs = ["listener_filter_buffer.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
    ],
)

envoy_cc_library(
    name = "transport_socket_interface",
    hdrs = ["transport_socket.h"],
    deps = [
        ":io_handle_interface",
        ":post_io_action_interface",
        ":proxy_protocol_options_lib",
        "//envoy/buffer:buffer_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/ssl:connection_interface",
        "//envoy/ssl:context_interface",
        "//envoy/stream_info:filter_state_interface",
    ],
)

envoy_cc_library(
    name = "post_io_action_interface",
    hdrs = ["post_io_action.h"],
    deps = [],
)

envoy_cc_library(
    name = "connection_balancer_interface",
    hdrs = ["connection_balancer.h"],
    deps = [
        ":listen_socket_interface",
    ],
)

envoy_cc_library(
    name = "listener_interface",
    hdrs = ["listener.h"],
    deps = [
        ":connection_balancer_interface",
        ":connection_interface",
        ":listen_socket_interface",
        ":udp_packet_writer_handler_interface",
        "//envoy/access_log:access_log_interface",
        "//envoy/common:resource_interface",
        "//envoy/config:typed_metadata_interface",
        "//envoy/init:manager_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:interval_value",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "resolver_interface",
    hdrs = ["resolver.h"],
    deps = [
        ":address_interface",
        "//envoy/config:typed_config_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "proxy_protocol_options_lib",
    hdrs = ["proxy_protocol.h"],
    deps = [
        ":address_interface",
    ],
)
