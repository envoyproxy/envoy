load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "listener_manager_lib",
    srcs = [
        "listener_impl.cc",
        "listener_manager_impl.cc",
    ],
    hdrs = [
        "listener_impl.h",
        "listener_manager_impl.h",
    ],
    # any changes to this should be reviewed by mobile maintainers
    # to ensure that listener code doesn't leak back into Envoy Mobile.
    extra_visibility = [
        "//source/server/admin:__subpackages__",
        "//source/server/config_validation:__subpackages__",
        "//test:__subpackages__",
    ],
    deps = [
        "//source/server:listener_manager_factory_lib",
        "//source/server:api_listener_lib",
        "//source/server:active_raw_udp_listener_config",
        "//source/server:configuration_lib",
        "//source/server:drain_manager_lib",
        "//source/server:filter_chain_manager_lib",
        "//source/server:lds_api_lib",
        "//source/server:transport_socket_config_lib",
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
        "//source/common/config:metadata_lib",
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
        "//source/common/stream_info:stream_info_lib",
        "//source/common/quic:quic_stat_names_lib",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/upstreams/http/generic:config",
        "//source/extensions/udp_packet_writer/default:config",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/listener/proxy_protocol/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/raw_buffer/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/udp_packet_writer/v3:pkg_cc_proto",
    ] + envoy_select_enable_http3([
        "//source/common/quic:active_quic_listener_lib",
        "//source/common/quic:client_connection_factory_lib",
        "//source/common/quic:quic_factory_lib",
        "//source/common/quic:quic_transport_socket_factory_lib",
        "//source/common/quic:udp_gso_batch_writer_lib",
        "//source/extensions/udp_packet_writer/gso:config",
    ]),
)
