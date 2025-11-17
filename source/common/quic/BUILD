load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "LEGACY_ALWAYSLINK",
)
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_enable_http3",
    "envoy_select_enable_http_datagrams",
)

licenses(["notice"])  # Apache 2

# TODO(mattklein123): Default visibility for this package should not be public. We should have
# default by private to within this package and package tests, and then only expose the libraries
# that are required to be selected into the build for http3 to work.
envoy_package()

envoy_cc_library(
    name = "envoy_quic_alarm_lib",
    srcs = envoy_select_enable_http3(["envoy_quic_alarm.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_quic_alarm.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "@com_github_google_quiche//:quic_core_alarm_lib",
        "@com_github_google_quiche//:quic_core_clock_lib",
        "@com_github_google_quiche//:quic_platform",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_alarm_factory_lib",
    srcs = envoy_select_enable_http3(["envoy_quic_alarm_factory.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_quic_alarm_factory.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_alarm_lib",
        "@com_github_google_quiche//:quic_core_alarm_factory_lib",
        "@com_github_google_quiche//:quic_core_arena_scoped_ptr_lib",
        "@com_github_google_quiche//:quic_core_one_block_arena_lib",
        "@com_github_google_quiche//:quic_platform",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_clock_lib",
    srcs = envoy_select_enable_http3(["envoy_quic_clock.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_quic_clock.h"]),
    visibility = ["//visibility:public"],
    deps = envoy_select_enable_http3([
        "//envoy/event:dispatcher_interface",
        "@com_github_google_quiche//:quic_core_clock_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_connection_debug_visitor_factory_interface",
    hdrs = envoy_select_enable_http3(["envoy_quic_connection_debug_visitor_factory_interface.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/common:optref_lib",
        "//envoy/common:pure_lib",
        "//envoy/config:typed_config_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/stream_info:stream_info_interface",
        "@com_github_google_quiche//:quic_core_connection_lib",
        "@com_github_google_quiche//:quic_core_session_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_connection_helper_lib",
    hdrs = envoy_select_enable_http3(["envoy_quic_connection_helper.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_clock_lib",
        "@com_github_google_quiche//:quic_core_connection_lib",
        "@com_github_google_quiche//:quiche_common_random_lib",
    ]),
)

envoy_cc_library(
    name = "quic_stat_names_lib",
    srcs = ["quic_stat_names.cc"],
    hdrs = ["quic_stat_names.h"],
    deps = select({
        "//bazel:disable_http3": [
            "//envoy/stats:stats_interface",
            "//source/common/stats:symbol_table_lib",
        ],
        "//conditions:default": [
            "//envoy/stats:stats_interface",
            "//source/common/stats:symbol_table_lib",
            "@com_github_google_quiche//:quic_core_error_codes_lib",
            "@com_github_google_quiche//:quic_core_types_lib",
        ],
    }),
)

envoy_cc_library(
    name = "envoy_quic_proof_source_base_lib",
    srcs = envoy_select_enable_http3(["envoy_quic_proof_source_base.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_quic_proof_source_base.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_utils_lib",
        "@com_github_google_quiche//:quic_core_crypto_certificate_view_lib",
        "@com_github_google_quiche//:quic_core_crypto_crypto_handshake_lib",
        "@com_github_google_quiche//:quic_core_crypto_proof_source_lib",
        "@com_github_google_quiche//:quic_core_data_lib",
        "@com_github_google_quiche//:quic_core_versions_lib",
        "@com_github_google_quiche//:quic_platform",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_proof_source_lib",
    srcs = envoy_select_enable_http3(["envoy_quic_proof_source.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_quic_proof_source.h"]),
    external_deps = ["ssl"],
    deps = envoy_select_enable_http3([
        ":envoy_quic_proof_source_base_lib",
        ":envoy_quic_utils_lib",
        ":quic_io_handle_wrapper_lib",
        ":quic_transport_socket_factory_lib",
        "//envoy/ssl:tls_certificate_config_interface",
        "//source/common/quic:cert_compression_lib",
        "//source/common/quic:quic_server_transport_socket_factory_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/server:listener_stats",
        "@com_github_google_quiche//:quic_core_crypto_certificate_view_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_proof_verifier_base_lib",
    srcs = envoy_select_enable_http3(["envoy_quic_proof_verifier_base.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_quic_proof_verifier_base.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_utils_lib",
        "@com_github_google_quiche//:quic_core_crypto_certificate_view_lib",
        "@com_github_google_quiche//:quic_core_crypto_crypto_handshake_lib",
        "@com_github_google_quiche//:quic_core_versions_lib",
        "@com_github_google_quiche//:quic_platform",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_proof_verifier_lib",
    srcs = envoy_select_enable_http3(["envoy_quic_proof_verifier.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_quic_proof_verifier.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_proof_verifier_base_lib",
        ":envoy_quic_utils_lib",
        ":quic_ssl_connection_info_lib",
        "//source/common/tls:context_lib",
        "@com_github_google_quiche//:quic_platform",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_stream_lib",
    srcs = envoy_select_enable_http3(["envoy_quic_stream.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_quic_stream.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_simulated_watermark_buffer_lib",
        ":envoy_quic_utils_lib",
        ":quic_filter_manager_connection_lib",
        ":quic_stats_gatherer",
        ":send_buffer_monitor_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/http:codec_interface",
        "//source/common/http:codec_helper_lib",
        "@com_github_google_quiche//:http2_adapter",
        "@com_github_google_quiche//:quic_core_http_client_lib",
        "@com_github_google_quiche//:quic_core_http_http_encoder_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ]) + envoy_select_enable_http_datagrams([
        ":http_datagram_handler",
    ]),
)

envoy_cc_library(
    name = "client_connection_factory_lib",
    srcs = envoy_select_enable_http3(["client_connection_factory_impl.cc"]),
    hdrs = envoy_select_enable_http3(["client_connection_factory_impl.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_alarm_factory_lib",
        ":envoy_quic_client_session_lib",
        ":envoy_quic_connection_helper_lib",
        ":envoy_quic_proof_verifier_lib",
        ":envoy_quic_utils_lib",
        ":quic_client_packet_writer_factory_impl_lib",
        "//envoy/http:codec_interface",
        "//envoy/http:persistent_quic_info_interface",
        "//envoy/registry",
        "//source/common/runtime:runtime_lib",
        "//source/common/tls:client_ssl_socket_lib",
        "//source/extensions/quic/crypto_stream:envoy_quic_crypto_client_stream_lib",
        "@com_github_google_quiche//:quic_core_http_spdy_session_lib",
    ]),
)

envoy_cc_library(
    name = "client_codec_lib",
    srcs = envoy_select_enable_http3(["client_codec_impl.cc"]),
    hdrs = envoy_select_enable_http3([
        "client_codec_impl.h",
        "codec_impl.h",
    ]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_client_session_lib",
        ":envoy_quic_utils_lib",
        "//envoy/http:codec_interface",
        "//envoy/registry",
        "@com_github_google_quiche//:quic_core_http_spdy_session_lib",
    ]),
)

envoy_cc_library(
    name = "server_codec_lib",
    srcs = envoy_select_enable_http3(["server_codec_impl.cc"]),
    hdrs = envoy_select_enable_http3([
        "codec_impl.h",
        "server_codec_impl.h",
    ]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_server_session_lib",
        ":envoy_quic_utils_lib",
        ":quic_server_factory_stub_lib",
        "//envoy/http:codec_interface",
        "//envoy/registry",
        "//envoy/server/overload:overload_manager_interface",
        "@com_github_google_quiche//:quic_core_http_spdy_session_lib",
    ]),
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_library(
    name = "quic_ssl_connection_info_lib",
    hdrs = envoy_select_enable_http3(["quic_ssl_connection_info.h"]),
    external_deps = ["ssl"],
    deps = envoy_select_enable_http3([
        "//source/common/tls:connection_info_impl_base_lib",
        "@com_github_google_quiche//:quic_core_session_lib",
    ]),
)

envoy_cc_library(
    name = "quic_filter_manager_connection_lib",
    srcs = envoy_select_enable_http3(["quic_filter_manager_connection_impl.cc"]),
    hdrs = envoy_select_enable_http3(["quic_filter_manager_connection_impl.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_simulated_watermark_buffer_lib",
        ":quic_network_connection_lib",
        ":quic_ssl_connection_info_lib",
        ":quic_stat_names_lib",
        ":send_buffer_monitor_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/http:header_map_lib",
        "//source/common/http/http3:codec_stats_lib",
        "//source/common/network:connection_base_lib",
        "//source/common/stream_info:stream_info_lib",
        "@com_github_google_quiche//:quic_core_connection_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_server_session_lib",
    srcs = envoy_select_enable_http3([
        "envoy_quic_server_session.cc",
        "envoy_quic_server_stream.cc",
    ]),
    hdrs = envoy_select_enable_http3([
        "envoy_quic_server_session.h",
        "envoy_quic_server_stream.h",
    ]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_connection_debug_visitor_factory_interface",
        ":envoy_quic_proof_source_lib",
        ":envoy_quic_server_connection_lib",
        ":envoy_quic_server_crypto_stream_factory_lib",
        ":envoy_quic_stream_lib",
        ":envoy_quic_utils_lib",
        ":quic_filter_manager_connection_lib",
        ":quic_stat_names_lib",
        ":quic_stats_gatherer",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/http:header_map_lib",
        "@com_github_google_quiche//:quic_server_http_spdy_session_lib",
        "@com_google_absl//absl/types:optional",
    ]) + envoy_select_enable_http_datagrams([
        ":http_datagram_handler",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_client_session_lib",
    srcs = envoy_select_enable_http3([
        "envoy_quic_client_session.cc",
        "envoy_quic_client_stream.cc",
        "quic_network_connectivity_observer_impl.cc",
    ]),
    hdrs = envoy_select_enable_http3([
        "envoy_quic_client_session.h",
        "envoy_quic_client_stream.h",
        "quic_network_connectivity_observer_impl.h",
    ]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_client_connection_lib",
        ":envoy_quic_client_crypto_stream_factory_lib",
        ":envoy_quic_proof_verifier_lib",
        ":envoy_quic_stream_lib",
        ":envoy_quic_utils_lib",
        ":quic_filter_manager_connection_lib",
        ":quic_network_connectivity_observer_interface",
        ":envoy_quic_network_observer_registry_factory_lib",
        ":quic_stat_names_lib",
        ":quic_transport_socket_factory_lib",
        "//envoy/http:http_server_properties_cache_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "@com_github_google_quiche//:quic_core_http_client_lib",
        "@com_github_google_quiche//:quic_core_error_codes_lib",
    ]) + envoy_select_enable_http_datagrams([
        ":http_datagram_handler",
    ]),
)

envoy_cc_library(
    name = "quic_network_connectivity_observer_interface",
    hdrs = [
        "quic_network_connectivity_observer.h",
    ],
    deps = envoy_select_enable_http3([
        "//source/common/common:logger_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_network_observer_registry_factory_lib",
    hdrs = [
        "envoy_quic_network_observer_registry_factory.h",
    ],
    deps = [
        ":quic_network_connectivity_observer_interface",
        "//envoy/event:dispatcher_interface",
    ],
)

envoy_cc_library(
    name = "quic_io_handle_wrapper_lib",
    hdrs = envoy_select_enable_http3(["quic_io_handle_wrapper.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/network:io_handle_interface",
        "//source/common/network:io_socket_error_lib",
    ]),
)

envoy_cc_library(
    name = "quic_network_connection_lib",
    srcs = envoy_select_enable_http3(["quic_network_connection.cc"]),
    hdrs = envoy_select_enable_http3(["quic_network_connection.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/network:connection_interface",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_server_connection_lib",
    srcs = envoy_select_enable_http3(["envoy_quic_server_connection.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_quic_server_connection.h"]),
    deps = envoy_select_enable_http3([
        ":quic_io_handle_wrapper_lib",
        ":quic_network_connection_lib",
        "//source/common/network:generic_listener_filter_impl_base_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/quic:envoy_quic_utils_lib",
        "@com_github_google_quiche//:quic_core_connection_lib",
        "@com_github_google_quiche//:quic_core_packet_writer_lib",
        "@com_github_google_quiche//:quic_core_packets_lib",
    ]),
)

envoy_cc_library(
    name = "quic_client_packet_writer_factory_impl_lib",
    srcs = envoy_select_enable_http3(["quic_client_packet_writer_factory_impl.cc"]),
    hdrs = envoy_select_enable_http3(["quic_client_packet_writer_factory_impl.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_packet_writer_lib",
        ":envoy_quic_utils_lib",
        ":envoy_quic_client_connection_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_client_connection_lib",
    srcs = envoy_select_enable_http3(["envoy_quic_client_connection.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_quic_client_connection.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_packet_writer_lib",
        ":quic_network_connection_lib",
        "//envoy/event:dispatcher_interface",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/runtime:runtime_lib",
        "@com_github_google_quiche//:quic_core_connection_lib",
        "@com_github_google_quiche//:quic_core_http_client_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_dispatcher_lib",
    srcs = envoy_select_enable_http3(["envoy_quic_dispatcher.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_quic_dispatcher.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_connection_debug_visitor_factory_interface",
        ":envoy_quic_proof_source_lib",
        ":envoy_quic_server_connection_lib",
        ":envoy_quic_server_crypto_stream_factory_lib",
        ":envoy_quic_server_session_lib",
        ":quic_stat_names_lib",
        "//envoy/network:listener_interface",
        "@com_github_google_quiche//:quic_core_server_lib",
        "@com_github_google_quiche//:quic_core_utils_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_simulated_watermark_buffer_lib",
    hdrs = envoy_select_enable_http3(["envoy_quic_simulated_watermark_buffer.h"]),
    deps = envoy_select_enable_http3(["//source/common/common:assert_lib"]),
)

envoy_cc_library(
    name = "active_quic_listener_lib",
    srcs = envoy_select_enable_http3(["active_quic_listener.cc"]),
    hdrs = envoy_select_enable_http3(["active_quic_listener.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_alarm_factory_lib",
        ":envoy_quic_connection_debug_visitor_factory_interface",
        ":envoy_quic_connection_helper_lib",
        ":envoy_quic_dispatcher_lib",
        ":envoy_quic_packet_writer_lib",
        ":envoy_quic_proof_source_factory_interface",
        ":envoy_quic_proof_source_lib",
        ":envoy_quic_server_preferred_address_config_factory_interface",
        ":envoy_quic_utils_lib",
        "//envoy/network:listener_interface",
        "//source/common/network:listener_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/quic/connection_id_generator/deterministic:envoy_deterministic_connection_id_generator_config",
        "//source/server:active_udp_listener",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/quic/connection_id_generator/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/quic/crypto_stream/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/quic/proof_source/v3:pkg_cc_proto",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_utils_lib",
    srcs = envoy_select_enable_http3(["envoy_quic_utils.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_quic_utils.h"]),
    external_deps = ["ssl"],
    deps = envoy_select_enable_http3([
        "//envoy/http:codec_interface",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:connection_socket_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/quic:quic_io_handle_wrapper_lib",
        "//source/common/runtime:runtime_lib",
        "@com_github_google_quiche//:quic_core_config_lib",
        "@com_github_google_quiche//:quic_core_http_header_list_lib",
        "@com_github_google_quiche//:quic_platform",
        "@com_github_google_quiche//:quic_core_http_client_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ]),
)

envoy_cc_library(
    name = "quic_transport_socket_factory_lib",
    srcs = envoy_select_enable_http3([
        "quic_client_transport_socket_factory.cc",
        "quic_transport_socket_factory.cc",
    ]),
    hdrs = envoy_select_enable_http3([
        "quic_client_transport_socket_factory.h",
        "quic_transport_socket_factory.h",
    ]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_proof_verifier_lib",
        "//envoy/network:transport_socket_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl:context_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/quic:cert_compression_lib",
        "//source/common/tls:client_ssl_socket_lib",
        "//source/common/tls:context_config_lib",
        "@com_github_google_quiche//:quic_core_crypto_crypto_handshake_lib",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
    ]),
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_library(
    name = "quic_server_transport_socket_factory_lib",
    srcs = envoy_select_enable_http3([
        "quic_server_transport_socket_factory.cc",
    ]),
    hdrs = envoy_select_enable_http3([
        "quic_server_transport_socket_factory.h",
    ]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_proof_verifier_lib",
        ":quic_transport_socket_factory_lib",
        "//envoy/network:transport_socket_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl:context_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/tls:server_context_config_lib",
        "//source/common/tls:server_context_lib",
        "//source/common/tls:server_ssl_socket_lib",
        "@com_github_google_quiche//:quic_core_crypto_crypto_handshake_lib",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
    ]),
    alwayslink = LEGACY_ALWAYSLINK,
)

# Create a single target that contains all the libraries that register client factories.
# All of these are needed for this extension to function.
envoy_cc_library(
    name = "quic_client_factory_lib",
    deps = envoy_select_enable_http3([
        ":client_codec_lib",
        ":quic_transport_socket_factory_lib",
        "//source/extensions/quic/proof_source:envoy_quic_proof_source_factory_impl_lib",
    ]),
)

# The factory for server connection creation.
envoy_cc_library(
    name = "quic_server_factory_stub_lib",
    hdrs = envoy_select_enable_http3(["server_connection_factory.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/http:codec_interface",
        "//envoy/network:connection_interface",
        "//source/common/http/http3:codec_stats_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ]),
)

# Create a single target that contains all the libraries that register factories.
# All of these are needed for this extension to function.
envoy_cc_library(
    name = "quic_server_factory_lib",
    deps = envoy_select_enable_http3([
        ":quic_transport_socket_factory_lib",
        ":server_codec_lib",
        "//source/extensions/quic/crypto_stream:envoy_quic_crypto_server_stream_lib",
        "//source/extensions/quic/proof_source:envoy_quic_proof_source_factory_impl_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_packet_writer_lib",
    srcs = envoy_select_enable_http3(["envoy_quic_packet_writer.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_quic_packet_writer.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_utils_lib",
        "@com_github_google_quiche//:quic_core_packet_writer_lib",
        "@com_github_google_quiche//:quic_platform",
    ]),
)

envoy_cc_library(
    name = "udp_gso_batch_writer_lib",
    srcs = select({
        "//bazel:http3_enabled_and_linux": ["udp_gso_batch_writer.cc"],
        "//conditions:default": [],
    }),
    hdrs = envoy_select_enable_http3(["udp_gso_batch_writer.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_utils_lib",
        "//envoy/network:udp_packet_writer_handler_interface",
        "//source/common/network:io_socket_error_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "@com_github_google_quiche//:quic_platform",
    ]) + select({
        "//bazel:http3_enabled_and_linux": ["@com_github_google_quiche//:quic_core_batch_writer_gso_batch_writer_lib"],
        "//conditions:default": [],
    }),
)

envoy_cc_library(
    name = "send_buffer_monitor_lib",
    srcs = envoy_select_enable_http3(["send_buffer_monitor.cc"]),
    hdrs = envoy_select_enable_http3(["send_buffer_monitor.h"]),
    deps = envoy_select_enable_http3([
        "//source/common/common:assert_lib",
        "@com_github_google_quiche//:quic_core_session_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_client_crypto_stream_factory_lib",
    hdrs = envoy_select_enable_http3(["envoy_quic_client_crypto_stream_factory.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/common:optref_lib",
        "//envoy/config:typed_config_interface",
        "//envoy/network:transport_socket_interface",
        "@com_github_google_quiche//:quic_client_session_lib",
        "@com_github_google_quiche//:quic_core_http_spdy_session_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_server_crypto_stream_factory_lib",
    hdrs = envoy_select_enable_http3(["envoy_quic_server_crypto_stream_factory.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/config:typed_config_interface",
        "@com_github_google_quiche//:quic_core_http_spdy_session_lib",
        "@com_github_google_quiche//:quic_server_session_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_proof_source_factory_interface",
    hdrs = envoy_select_enable_http3(["envoy_quic_proof_source_factory_interface.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/config:typed_config_interface",
        "@com_github_google_quiche//:quic_core_crypto_proof_source_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_connection_id_generator_factory_interface",
    hdrs = envoy_select_enable_http3(["envoy_quic_connection_id_generator_factory.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/config:typed_config_interface",
        "@com_github_google_quiche//:quic_core_connection_id_generator_interface_lib",
        "@com_github_google_quiche//:quic_load_balancer_encoder_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_deterministic_connection_id_generator_lib",
    srcs = envoy_select_enable_http3(["envoy_deterministic_connection_id_generator.cc"]),
    hdrs = envoy_select_enable_http3(["envoy_deterministic_connection_id_generator.h"]),
    deps = envoy_select_enable_http3([
        ":envoy_quic_connection_id_generator_factory_interface",
        ":envoy_quic_utils_lib",
        "@com_github_google_quiche//:quic_core_deterministic_connection_id_generator_lib",
    ]),
)

envoy_cc_library(
    name = "envoy_quic_server_preferred_address_config_factory_interface",
    hdrs = envoy_select_enable_http3(["envoy_quic_server_preferred_address_config_factory.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/config:typed_config_interface",
        "//envoy/network:address_interface",
        "//envoy/server:factory_context_interface",
        "@com_github_google_quiche//:quic_platform_socket_address",
    ]),
)

envoy_cc_library(
    name = "quic_stats_gatherer",
    srcs = envoy_select_enable_http3(["quic_stats_gatherer.cc"]),
    hdrs = envoy_select_enable_http3(["quic_stats_gatherer.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/access_log:access_log_interface",
        "//envoy/formatter:http_formatter_context_interface",
        "//envoy/http:codec_interface",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:header_map_lib",
        "@com_github_google_quiche//:quic_core_ack_listener_interface_lib",
    ]),
)

envoy_cc_library(
    name = "http_datagram_handler",
    srcs = envoy_select_enable_http3(["http_datagram_handler.cc"]),
    hdrs = envoy_select_enable_http3(["http_datagram_handler.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/http:codec_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/http:header_map_lib",
        "@com_github_google_quiche//:quic_core_http_spdy_session_lib",
        "@com_github_google_quiche//:quic_core_types_lib",
    ]),
)

envoy_cc_library(
    name = "cert_compression_lib",
    srcs = envoy_select_enable_http3(["cert_compression.cc"]),
    hdrs = envoy_select_enable_http3(["cert_compression.h"]),
    external_deps = ["ssl"],
    deps = envoy_select_enable_http3([
        "//bazel/foreign_cc:zlib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/runtime:runtime_lib",
    ]),
)
