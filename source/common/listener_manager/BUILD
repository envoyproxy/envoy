load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "listener_manager_lib",
    srcs = [
        "listener_impl.cc",
        "listener_manager_impl.cc",
    ],
    hdrs = [
        "listener_impl.h",
        "listener_manager_impl.h",
    ],
    rbe_pool = "2core",
    deps = [
        ":active_raw_udp_listener_config",
        ":connection_handler_lib",
        ":filter_chain_manager_lib",
        ":lds_api_lib",
        ":listener_info_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/config:typed_metadata_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:listener_interface",
        "//envoy/server:api_listener_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/server:listener_manager_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/server:worker_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/common:basic_resource_lib",
        "//source/common/common:empty_string",
        "//source/common/config:utility_lib",
        "//source/common/http:conn_manager_lib",
        "//source/common/init:manager_lib",
        "//source/common/init:target_lib",
        "//source/common/network:connection_balancer_lib",
        "//source/common/network:filter_matcher_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:listener_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/quic:quic_stat_names_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/udp_packet_writer/default:config",
        "//source/extensions/upstreams/http/generic:config",
        "//source/server:api_listener_lib",
        "//source/server:configuration_lib",
        "//source/server:drain_manager_lib",
        "//source/server:factory_context_lib",
        "//source/server:listener_manager_factory_lib",
        "//source/server:transport_socket_config_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/listener/proxy_protocol/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/raw_buffer/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/udp_packet_writer/v3:pkg_cc_proto",
    ] + envoy_select_enable_http3([
        "//source/common/quic:active_quic_listener_lib",
        "//source/common/quic:client_connection_factory_lib",
        "//source/common/quic:quic_server_factory_lib",
        "//source/common/quic:quic_server_transport_socket_factory_lib",
        "//source/common/quic:quic_transport_socket_factory_lib",
        "//source/common/quic:udp_gso_batch_writer_lib",
        "//source/extensions/udp_packet_writer/gso:config",
    ]),
    alwayslink = True,
)

envoy_cc_library(
    name = "active_raw_udp_listener_config",
    srcs = ["active_raw_udp_listener_config.cc"],
    hdrs = ["active_raw_udp_listener_config.h"],
    deps = [
        ":connection_handler_lib",
        "//envoy/registry",
    ],
)

envoy_cc_library(
    name = "filter_chain_manager_lib",
    srcs = ["filter_chain_manager_impl.cc"],
    hdrs = ["filter_chain_manager_impl.h"],
    deps = [
        ":filter_chain_factory_context_callback",
        "//envoy/config:typed_metadata_interface",
        "//envoy/matcher:matcher_interface",
        "//envoy/network:filter_interface",
        "//envoy/server:instance_interface",
        "//envoy/server:listener_manager_interface",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/common:empty_string",
        "//source/common/config:utility_lib",
        "//source/common/init:manager_lib",
        "//source/common/matcher:matcher_lib",
        "//source/common/network:cidr_range_lib",
        "//source/common/network:lc_trie_lib",
        "//source/common/network/matching:data_impl_lib",
        "//source/common/network/matching:inputs_lib",
        "//source/server:configuration_lib",
        "//source/server:factory_context_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_chain_factory_context_callback",
    hdrs = ["filter_chain_factory_context_callback.h"],
    deps = [
        "//envoy/server:filter_config_interface",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "lds_api_lib",
    srcs = ["lds_api.cc"],
    hdrs = ["lds_api.h"],
    deps = [
        "//envoy/config:subscription_factory_interface",
        "//envoy/config:subscription_interface",
        "//envoy/init:manager_interface",
        "//envoy/server:listener_manager_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/config:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/init:target_lib",
        "//source/common/protobuf:utility_lib",
        "@com_google_absl//absl/container:node_hash_set",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "connection_handler_lib",
    srcs = ["connection_handler_impl.cc"],
    hdrs = [
        "connection_handler_impl.h",
    ],
    deps = [
        "active_tcp_listener",
        "//envoy/common:time_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:client_connection_factory",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:listener_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/server:listener_manager_interface",
        "//envoy/stats:timespan_interface",
        "//source/common/common:linked_object",
        "//source/common/common:non_copyable",
        "//source/common/event:deferred_task",
        "//source/common/network:address_lib",
        "//source/common/network:connection_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/server:active_udp_listener",
        "//source/server:listener_manager_factory_lib",
    ],
)

envoy_cc_library(
    name = "active_tcp_listener",
    srcs = ["active_tcp_listener.cc"],
    hdrs = [
        "active_tcp_listener.h",
    ],
    deps = [
        ":active_stream_listener_base",
        ":active_tcp_socket",
        "//envoy/common:time_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:listener_interface",
        "//envoy/server:listener_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/network:connection_lib",
        "//source/common/network:listener_lib",
        "//source/common/stats:timespan_lib",
        "//source/server:active_listener_base",
    ],
)

envoy_cc_library(
    name = "active_tcp_listener_headers",
    hdrs = [
        "active_stream_listener_base.h",
        "active_tcp_listener.h",
        "active_tcp_socket.h",
    ],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:listener_interface",
        "//envoy/server:listener_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:generic_listener_filter_impl_base_lib",
        "//source/common/stats:timespan_lib",
        "//source/server:active_listener_base",
    ],
)

envoy_cc_library(
    name = "listener_info_lib",
    srcs = ["listener_info_impl.cc"],
    hdrs = [
        "listener_info_impl.h",
    ],
    deps = [
        "//envoy/network:listener_interface",
        "//source/common/config:metadata_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "active_tcp_socket",
    srcs = ["active_tcp_socket.cc"],
    hdrs = [
        "active_tcp_socket.h",
    ],
    deps = [
        ":active_tcp_listener_headers",
        "//envoy/common:time_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:listener_interface",
        "//source/common/common:linked_object",
        "//source/common/network:connection_lib",
        "//source/common/network:listener_filter_buffer_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/server:active_listener_base",
    ],
)

envoy_cc_library(
    name = "active_stream_listener_base",
    srcs = ["active_stream_listener_base.cc"],
    hdrs = [
        "active_stream_listener_base.h",
    ],
    deps = [
        ":active_tcp_listener_headers",
        "//envoy/common:time_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:listener_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:linked_object",
        "//source/common/network:connection_lib",
        "//source/common/network:listener_filter_buffer_lib",
        "//source/server:active_listener_base",
    ],
)
