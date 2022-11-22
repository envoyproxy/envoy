load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)
load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "LEGACY_ALWAYSLINK",
)

licenses(["notice"])  # Apache 2

# TODO(mattklein123): Default visibility for this package should not be public. We should have
# default by private to within this package and package tests, and then only expose the libraries
# that are required to be selected into the build for http3 to work.
envoy_package()

envoy_cc_library(
    name = "envoy_quic_alarm_lib",
    srcs = ["envoy_quic_alarm.cc"],
    hdrs = ["envoy_quic_alarm.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "@com_github_google_quiche//:quic_core_alarm_lib",
        "@com_github_google_quiche//:quic_core_clock_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_alarm_factory_lib",
    srcs = ["envoy_quic_alarm_factory.cc"],
    hdrs = ["envoy_quic_alarm_factory.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_alarm_lib",
        "@com_github_google_quiche//:quic_core_alarm_factory_lib",
        "@com_github_google_quiche//:quic_core_arena_scoped_ptr_lib",
        "@com_github_google_quiche//:quic_core_one_block_arena_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_clock_lib",
    srcs = ["envoy_quic_clock.cc"],
    hdrs = ["envoy_quic_clock.h"],
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "@com_github_google_quiche//:quic_core_clock_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_connection_helper_lib",
    hdrs = ["envoy_quic_connection_helper.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_clock_lib",
        "@com_github_google_quiche//:quic_core_connection_lib",
        "@com_github_google_quiche//:quiche_common_random_lib",
    ],
)

envoy_cc_library(
    name = "quic_stat_names_lib",
    srcs = ["quic_stat_names.cc"],
    hdrs = ["quic_stat_names.h"],
    tags = ["nofips"],
    deps = [
        "//envoy/stats:stats_interface",
        "//source/common/stats:symbol_table_lib",
        "@com_github_google_quiche//:quic_core_error_codes_lib",
        "@com_github_google_quiche//:quic_core_types_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_proof_source_base_lib",
    srcs = ["envoy_quic_proof_source_base.cc"],
    hdrs = ["envoy_quic_proof_source_base.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_utils_lib",
        "@com_github_google_quiche//:quic_core_crypto_certificate_view_lib",
        "@com_github_google_quiche//:quic_core_crypto_crypto_handshake_lib",
        "@com_github_google_quiche//:quic_core_crypto_proof_source_lib",
        "@com_github_google_quiche//:quic_core_data_lib",
        "@com_github_google_quiche//:quic_core_versions_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_proof_source_lib",
    srcs = ["envoy_quic_proof_source.cc"],
    hdrs = ["envoy_quic_proof_source.h"],
    external_deps = ["ssl"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_proof_source_base_lib",
        ":envoy_quic_utils_lib",
        ":quic_io_handle_wrapper_lib",
        ":quic_transport_socket_factory_lib",
        "//envoy/ssl:tls_certificate_config_interface",
        "//source/common/stream_info:stream_info_lib",
        "//source/server:connection_handler_lib",
        "@com_github_google_quiche//:quic_core_crypto_certificate_view_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_proof_verifier_base_lib",
    srcs = ["envoy_quic_proof_verifier_base.cc"],
    hdrs = ["envoy_quic_proof_verifier_base.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_utils_lib",
        "@com_github_google_quiche//:quic_core_crypto_certificate_view_lib",
        "@com_github_google_quiche//:quic_core_crypto_crypto_handshake_lib",
        "@com_github_google_quiche//:quic_core_versions_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_proof_verifier_lib",
    srcs = ["envoy_quic_proof_verifier.cc"],
    hdrs = ["envoy_quic_proof_verifier.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_proof_verifier_base_lib",
        ":envoy_quic_utils_lib",
        ":quic_ssl_connection_info_lib",
        "//source/extensions/transport_sockets/tls:context_lib",
    ],
)

envoy_cc_library(
    name = "spdy_server_push_utils_for_envoy_lib",
    srcs = ["spdy_server_push_utils_for_envoy.cc"],
    tags = ["nofips"],
    deps = [
        "//source/common/common:assert_lib",
        "@com_github_google_quiche//:quic_core_http_spdy_server_push_utils_header",
    ],
)

envoy_cc_library(
    name = "envoy_quic_stream_lib",
    hdrs = ["envoy_quic_stream.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_simulated_watermark_buffer_lib",
        ":envoy_quic_utils_lib",
        ":quic_filter_manager_connection_lib",
        ":send_buffer_monitor_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/http:codec_interface",
        "//source/common/http:codec_helper_lib",
        "@com_github_google_quiche//:http2_adapter",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "client_connection_factory_lib",
    srcs = ["client_connection_factory_impl.cc"],
    hdrs = ["client_connection_factory_impl.h"],
    tags = ["nofips"],
    deps = [
        ":active_quic_listener_lib",
        ":envoy_quic_alarm_factory_lib",
        ":envoy_quic_client_session_lib",
        ":envoy_quic_connection_helper_lib",
        ":envoy_quic_proof_verifier_lib",
        ":envoy_quic_server_session_lib",
        ":envoy_quic_utils_lib",
        "//envoy/http:codec_interface",
        "//envoy/http:persistent_quic_info_interface",
        "//envoy/registry",
        "//source/extensions/quic/crypto_stream:envoy_quic_crypto_client_stream_lib",
        "//source/extensions/transport_sockets/tls:ssl_socket_lib",
        "@com_github_google_quiche//:quic_core_http_spdy_session_lib",
    ],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_client_session_lib",
        ":envoy_quic_server_session_lib",
        ":envoy_quic_utils_lib",
        "//envoy/http:codec_interface",
        "//envoy/registry",
        "@com_github_google_quiche//:quic_core_http_spdy_session_lib",
    ],
)

envoy_cc_library(
    name = "quic_ssl_connection_info_lib",
    hdrs = ["quic_ssl_connection_info.h"],
    external_deps = ["ssl"],
    tags = ["nofips"],
    deps = [
        "//source/extensions/transport_sockets/tls:connection_info_impl_base_lib",
        "@com_github_google_quiche//:quic_core_session_lib",
    ],
)

envoy_cc_library(
    name = "quic_filter_manager_connection_lib",
    srcs = ["quic_filter_manager_connection_impl.cc"],
    hdrs = ["quic_filter_manager_connection_impl.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_simulated_watermark_buffer_lib",
        ":quic_network_connection_lib",
        ":quic_ssl_connection_info_lib",
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
    ],
)

envoy_cc_library(
    name = "envoy_quic_server_session_lib",
    srcs = [
        "envoy_quic_server_session.cc",
        "envoy_quic_server_stream.cc",
    ],
    hdrs = [
        "envoy_quic_server_session.h",
        "envoy_quic_server_stream.h",
    ],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_crypto_stream_factory_lib",
        ":envoy_quic_proof_source_lib",
        ":envoy_quic_server_connection_lib",
        ":envoy_quic_stream_lib",
        ":envoy_quic_utils_lib",
        ":quic_filter_manager_connection_lib",
        ":quic_stat_names_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/http:header_map_lib",
        "@com_github_google_quiche//:quic_core_http_spdy_session_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_client_session_lib",
    srcs = [
        "envoy_quic_client_session.cc",
        "envoy_quic_client_stream.cc",
    ],
    hdrs = [
        "envoy_quic_client_session.h",
        "envoy_quic_client_stream.h",
    ],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_client_connection_lib",
        ":envoy_quic_crypto_stream_factory_lib",
        ":envoy_quic_proof_verifier_lib",
        ":envoy_quic_stream_lib",
        ":envoy_quic_utils_lib",
        ":quic_filter_manager_connection_lib",
        ":quic_stat_names_lib",
        ":quic_transport_socket_factory_lib",
        "//envoy/http:http_server_properties_cache_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "@com_github_google_quiche//:quic_core_http_client_lib",
    ],
)

envoy_cc_library(
    name = "quic_io_handle_wrapper_lib",
    hdrs = ["quic_io_handle_wrapper.h"],
    deps = [
        "//envoy/network:io_handle_interface",
        "//source/common/network:io_socket_error_lib",
    ],
)

envoy_cc_library(
    name = "quic_network_connection_lib",
    srcs = ["quic_network_connection.cc"],
    hdrs = ["quic_network_connection.h"],
    tags = ["nofips"],
    deps = [
        "//envoy/network:connection_interface",
        "//source/common/network:listen_socket_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_server_connection_lib",
    srcs = ["envoy_quic_server_connection.cc"],
    hdrs = ["envoy_quic_server_connection.h"],
    tags = ["nofips"],
    deps = [
        ":quic_io_handle_wrapper_lib",
        ":quic_network_connection_lib",
        "//source/common/quic:envoy_quic_utils_lib",
        "//source/server:connection_handler_lib",
        "@com_github_google_quiche//:quic_core_connection_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_client_connection_lib",
    srcs = ["envoy_quic_client_connection.cc"],
    hdrs = ["envoy_quic_client_connection.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_packet_writer_lib",
        ":quic_network_connection_lib",
        "//envoy/event:dispatcher_interface",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "@com_github_google_quiche//:quic_core_connection_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "envoy_quic_dispatcher_lib",
    srcs = ["envoy_quic_dispatcher.cc"],
    hdrs = ["envoy_quic_dispatcher.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_crypto_stream_factory_lib",
        ":envoy_quic_proof_source_lib",
        ":envoy_quic_server_connection_lib",
        ":envoy_quic_server_session_lib",
        ":quic_stat_names_lib",
        "//envoy/network:listener_interface",
        "//source/server:connection_handler_lib",
        "@com_github_google_quiche//:quic_core_server_lib",
        "@com_github_google_quiche//:quic_core_utils_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_simulated_watermark_buffer_lib",
    hdrs = ["envoy_quic_simulated_watermark_buffer.h"],
    deps = ["//source/common/common:assert_lib"],
)

envoy_cc_library(
    name = "active_quic_listener_lib",
    srcs = ["active_quic_listener.cc"],
    hdrs = ["active_quic_listener.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_alarm_factory_lib",
        ":envoy_quic_connection_helper_lib",
        ":envoy_quic_dispatcher_lib",
        ":envoy_quic_packet_writer_lib",
        ":envoy_quic_proof_source_factory_interface",
        ":envoy_quic_proof_source_lib",
        ":envoy_quic_utils_lib",
        "//envoy/network:listener_interface",
        "//source/common/network:listener_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/quic/connection_id_generator:envoy_deterministic_connection_id_generator_config",
        "//source/server:connection_handler_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/quic/connection_id_generator/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/quic/crypto_stream/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/quic/proof_source/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "envoy_quic_utils_lib",
    srcs = ["envoy_quic_utils.cc"],
    hdrs = ["envoy_quic_utils.h"],
    external_deps = [
        "quiche_quic_platform",
        "ssl",
    ],
    tags = ["nofips"],
    deps = [
        "//envoy/http:codec_interface",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/quic:quic_io_handle_wrapper_lib",
        "@com_github_google_quiche//:quic_core_config_lib",
        "@com_github_google_quiche//:quic_core_http_header_list_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "quic_transport_socket_factory_lib",
    srcs = ["quic_transport_socket_factory.cc"],
    hdrs = ["quic_transport_socket_factory.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_proof_verifier_lib",
        "//envoy/network:transport_socket_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl:context_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/extensions/transport_sockets/tls:context_config_lib",
        "//source/extensions/transport_sockets/tls:ssl_socket_lib",
        "@com_github_google_quiche//:quic_core_crypto_crypto_handshake_lib",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

# Create a single target that contains all the libraries that register factories.
# All of these are needed for this extension to function.
envoy_cc_library(
    name = "quic_factory_lib",
    tags = ["nofips"],
    # QUICHE can't build against FIPS BoringSSL until the FIPS build
    # is on a new enough version to have QUIC support. Remove it from
    # the build until then. Re-enable as part of #7433.
    deps = select({
        "//bazel:boringssl_fips": [],
        "//bazel:boringssl_disabled": [],
        "//conditions:default": [
            ":codec_lib",
            ":quic_transport_socket_factory_lib",
            "//source/extensions/quic/crypto_stream:envoy_quic_crypto_server_stream_lib",
            "//source/extensions/quic/proof_source:envoy_quic_proof_source_factory_impl_lib",
        ],
    }),
)

envoy_cc_library(
    name = "envoy_quic_packet_writer_lib",
    srcs = ["envoy_quic_packet_writer.cc"],
    hdrs = ["envoy_quic_packet_writer.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_utils_lib",
        "@com_github_google_quiche//:quic_core_packet_writer_lib",
    ],
)

envoy_cc_library(
    name = "udp_gso_batch_writer_lib",
    srcs = select({
        "//bazel:linux": ["udp_gso_batch_writer.cc"],
        "//conditions:default": [],
    }),
    hdrs = ["udp_gso_batch_writer.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_utils_lib",
        "//envoy/network:udp_packet_writer_handler_interface",
        "//source/common/network:io_socket_error_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "@com_github_google_quiche//:quic_core_batch_writer_gso_batch_writer_lib",
    ],
)

envoy_cc_library(
    name = "send_buffer_monitor_lib",
    srcs = ["send_buffer_monitor.cc"],
    hdrs = ["send_buffer_monitor.h"],
    tags = ["nofips"],
    deps = [
        "@com_github_google_quiche//:quic_core_session_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_crypto_stream_factory_lib",
    hdrs = ["envoy_quic_crypto_stream_factory.h"],
    tags = ["nofips"],
    deps = [
        "//envoy/config:typed_config_interface",
        "@com_github_google_quiche//:quic_core_http_spdy_session_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_proof_source_factory_interface",
    hdrs = ["envoy_quic_proof_source_factory_interface.h"],
    tags = ["nofips"],
    deps = [
        "//envoy/config:typed_config_interface",
        "//source/server:connection_handler_lib",
        "@com_github_google_quiche//:quic_core_crypto_proof_source_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_connection_id_generator_factory_interface",
    hdrs = ["envoy_quic_connection_id_generator_factory.h"],
    tags = ["nofips"],
    deps = [
        "//envoy/config:typed_config_interface",
        "@com_github_google_quiche//:quic_core_connection_id_generator_interface_lib",
        "@com_github_google_quiche//:quic_load_balancer_encoder_lib",
    ],
)
