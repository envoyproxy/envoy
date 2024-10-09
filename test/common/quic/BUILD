load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
    "envoy_select_enable_http_datagrams",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "envoy_quic_alarm_test",
    srcs = ["envoy_quic_alarm_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        "//source/common/quic:envoy_quic_alarm_factory_lib",
        "//source/common/quic:envoy_quic_alarm_lib",
        "//source/common/quic:envoy_quic_clock_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@com_github_google_quiche//:quic_platform",
    ],
)

envoy_cc_test(
    name = "envoy_quic_clock_test",
    srcs = ["envoy_quic_clock_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        "//source/common/quic:envoy_quic_clock_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_writer_test",
    srcs = ["envoy_quic_writer_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        "//source/common/network:io_socket_error_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/quic:envoy_quic_packet_writer_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@com_github_google_quiche//:quic_platform",
    ],
)

envoy_cc_test(
    name = "envoy_quic_proof_source_test",
    srcs = ["envoy_quic_proof_source_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        ":test_utils_lib",
        "//source/common/quic:envoy_quic_proof_source_lib",
        "//source/common/quic:envoy_quic_proof_verifier_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:server_context_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:test_runtime_lib",
        "@com_github_google_quiche//:quic_core_versions_lib",
        "@com_github_google_quiche//:quic_platform",
        "@com_github_google_quiche//:quic_test_tools_test_certificates_lib",
    ],
)

envoy_cc_test(
    name = "quic_filter_manager_connection_impl_test",
    srcs = ["quic_filter_manager_connection_impl_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        "//source/common/quic:quic_filter_manager_connection_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:utility_lib",
        "@com_github_google_quiche//:quic_test_tools_test_utils_lib",
    ],
)

envoy_cc_test(
    name = "quic_stat_names_test",
    srcs = ["quic_stat_names_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        "//source/common/quic:quic_stat_names_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_proof_verifier_test",
    srcs = ["envoy_quic_proof_verifier_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        ":test_utils_lib",
        "//source/common/quic:envoy_quic_proof_verifier_lib",
        "//source/common/tls:context_config_lib",
        "//test/common/config:dummy_config_proto_cc_proto",
        "//test/common/tls/cert_validator:timed_cert_validator",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "@com_github_google_quiche//:quic_platform",
        "@com_github_google_quiche//:quic_test_tools_test_certificates_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_server_stream_test",
    srcs = ["envoy_quic_server_stream_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        ":test_utils_lib",
        "//source/common/http:headers_lib",
        "//source/common/quic:envoy_quic_alarm_factory_lib",
        "//source/common/quic:envoy_quic_connection_helper_lib",
        "//source/common/quic:envoy_quic_server_connection_lib",
        "//source/common/quic:envoy_quic_server_session_lib",
        "//source/server:active_listener_base",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:stream_decoder_mock",
        "//test/mocks/network:network_mocks",
        "//test/test_common:utility_lib",
        "@com_github_google_quiche//:quic_core_http_spdy_session_lib",
        "@com_github_google_quiche//:quic_test_tools_qpack_qpack_test_utils_lib",
        "@com_github_google_quiche//:quic_test_tools_session_peer_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_client_stream_test",
    srcs = ["envoy_quic_client_stream_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        ":test_utils_lib",
        "//source/common/http:headers_lib",
        "//source/common/quic:envoy_quic_alarm_factory_lib",
        "//source/common/quic:envoy_quic_client_connection_lib",
        "//source/common/quic:envoy_quic_client_session_lib",
        "//source/common/quic:envoy_quic_connection_helper_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:stream_decoder_mock",
        "//test/mocks/network:network_mocks",
        "//test/test_common:utility_lib",
        "@com_github_google_quiche//:quic_core_http_spdy_session_lib",
        "@com_github_google_quiche//:quic_test_tools_qpack_qpack_test_utils_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_server_session_test",
    srcs = ["envoy_quic_server_session_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        ":test_proof_source_lib",
        ":test_utils_lib",
        "//envoy/stats:stats_macros",
        "//source/common/quic:envoy_quic_alarm_factory_lib",
        "//source/common/quic:envoy_quic_connection_helper_lib",
        "//source/common/quic:envoy_quic_server_connection_lib",
        "//source/common/quic:envoy_quic_server_session_lib",
        "//source/common/quic:server_codec_lib",
        "//source/server:configuration_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:stream_decoder_mock",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:global_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:simulated_time_system_lib",
        "@com_github_google_quiche//:quic_test_tools_config_peer_lib",
        "@com_github_google_quiche//:quic_test_tools_server_session_base_peer",
        "@com_github_google_quiche//:quic_test_tools_test_utils_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_client_session_test",
    srcs = ["envoy_quic_client_session_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        ":test_utils_lib",
        "//envoy/stats:stats_macros",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/quic:client_codec_lib",
        "//source/common/quic:envoy_quic_alarm_factory_lib",
        "//source/common/quic:envoy_quic_client_connection_lib",
        "//source/common/quic:envoy_quic_client_session_lib",
        "//source/common/quic:envoy_quic_connection_helper_lib",
        "//source/extensions/quic/crypto_stream:envoy_quic_crypto_client_stream_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:stream_decoder_mock",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@com_github_google_quiche//:quic_test_tools_session_peer_lib",
    ],
)

envoy_cc_test(
    name = "active_quic_listener_test",
    srcs = ["active_quic_listener_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        ":test_proof_source_lib",
        ":test_utils_lib",
        "//source/common/http:utility_lib",
        "//source/common/listener_manager:connection_handler_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/quic:active_quic_listener_lib",
        "//source/common/quic:envoy_quic_utils_lib",
        "//source/common/quic:udp_gso_batch_writer_lib",
        "//source/extensions/quic/crypto_stream:envoy_quic_crypto_server_stream_lib",
        "//source/extensions/quic/proof_source:envoy_quic_proof_source_factory_impl_lib",
        "//source/server:configuration_lib",
        "//source/server:process_context_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:network_utility_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "@com_github_google_quiche//:quic_test_tools_crypto_server_config_peer_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "envoy_quic_dispatcher_test",
    srcs = ["envoy_quic_dispatcher_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        ":test_proof_source_lib",
        ":test_utils_lib",
        "//envoy/stats:stats_macros",
        "//source/common/listener_manager:connection_handler_lib",
        "//source/common/quic:envoy_quic_alarm_factory_lib",
        "//source/common/quic:envoy_quic_connection_helper_lib",
        "//source/common/quic:envoy_quic_dispatcher_lib",
        "//source/common/quic:envoy_quic_proof_source_lib",
        "//source/common/quic:envoy_quic_server_session_lib",
        "//source/common/quic:quic_server_transport_socket_factory_lib",
        "//source/extensions/quic/crypto_stream:envoy_quic_crypto_server_stream_lib",
        "//source/server:configuration_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:global_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test_library(
    name = "test_proof_source_lib",
    hdrs = ["test_proof_source.h"],
    tags = ["nofips"],
    deps = [
        "//source/common/quic:envoy_quic_proof_source_base_lib",
        "//test/mocks/network:network_mocks",
        "@com_github_google_quiche//:quic_test_tools_test_certificates_lib",
    ],
)

envoy_cc_test_library(
    name = "test_proof_verifier_lib",
    hdrs = ["test_proof_verifier.h"],
    tags = ["nofips"],
    deps = [
        "//source/common/quic:envoy_quic_proof_verifier_base_lib",
    ],
)

envoy_cc_test(
    name = "client_connection_factory_impl_test",
    srcs = ["client_connection_factory_impl_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/http/http3:conn_pool_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/common/http:common_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:http_server_properties_cache_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:transport_socket_match_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test(
    name = "quic_io_handle_wrapper_test",
    srcs = ["quic_io_handle_wrapper_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        "//source/common/quic:quic_io_handle_wrapper_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_utils_test",
    srcs = ["envoy_quic_utils_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        "//source/common/quic:envoy_quic_utils_lib",
        "//test/mocks/api:api_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@com_github_google_quiche//:quic_test_tools_test_utils_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_simulated_watermark_buffer_test",
    srcs = ["envoy_quic_simulated_watermark_buffer_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = ["//source/common/quic:envoy_quic_simulated_watermark_buffer_lib"],
)

envoy_cc_test_library(
    name = "test_utils_lib",
    hdrs = ["test_utils.h"],
    external_deps = ["bazel_runfiles"],
    tags = ["nofips"],
    deps = [
        "//envoy/stream_info:stream_info_interface",
        "//source/common/quic:envoy_quic_client_connection_lib",
        "//source/common/quic:envoy_quic_client_session_lib",
        "//source/common/quic:envoy_quic_connection_debug_visitor_factory_interface",
        "//source/common/quic:envoy_quic_proof_verifier_lib",
        "//source/common/quic:envoy_quic_server_connection_lib",
        "//source/common/quic:quic_filter_manager_connection_lib",
        "//test/common/config:dummy_config_proto_cc_proto",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@com_github_google_quiche//:quic_core_http_spdy_session_lib",
        "@com_github_google_quiche//:quic_test_tools_first_flight_lib",
        "@com_github_google_quiche//:quic_test_tools_qpack_qpack_test_utils_lib",
    ],
)

envoy_cc_test(
    name = "quic_transport_socket_factory_test",
    srcs = ["quic_transport_socket_factory_test.cc"],
    data = [
        "//test/common/tls/test_data:certs",
    ],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        "//source/common/quic:quic_server_transport_socket_factory_lib",
        "//source/common/quic:quic_transport_socket_factory_lib",
        "//source/common/tls:context_config_lib",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "http_datagram_handler_test",
    srcs = envoy_select_enable_http_datagrams(["http_datagram_handler_test.cc"]),
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = envoy_select_enable_http_datagrams([
        "//source/common/quic:http_datagram_handler",
        "//test/mocks/buffer:buffer_mocks",
        "//test/test_common:utility_lib",
        "@com_github_google_quiche//:quic_test_tools_test_utils_lib",
    ]),
)

envoy_cc_test(
    name = "cert_compression_test",
    srcs = ["cert_compression_test.cc"],
    deps = [
        "//source/common/quic:cert_compression_lib",
        "//test/test_common:logging_lib",
    ],
)

envoy_proto_library(
    name = "envoy_quic_h3_fuzz_proto",
    srcs = ["envoy_quic_h3_fuzz.proto"],
    deps = ["//test/fuzz:common_proto"],
)

envoy_cc_test_library(
    name = "envoy_quic_h3_fuzz_helper_lib",
    srcs = ["envoy_quic_h3_fuzz_helper.cc"],
    hdrs = ["envoy_quic_h3_fuzz_helper.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_h3_fuzz_proto_cc_proto",
        "//source/common/common:assert_lib",
        "@com_github_google_quiche//:quic_test_tools_test_utils_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "envoy_quic_h3_fuzz_test",
    srcs = ["envoy_quic_h3_fuzz_test.cc"],
    corpus = "envoy_quic_h3_fuzz_test_corpus",
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        ":envoy_quic_h3_fuzz_helper_lib",
        ":test_proof_source_lib",
        ":test_utils_lib",
        "//source/common/quic:envoy_quic_alarm_factory_lib",
        "//source/common/quic:envoy_quic_connection_helper_lib",
        "//source/common/quic:envoy_quic_dispatcher_lib",
        "//source/common/quic:server_codec_lib",
        "//test/mocks/http:http_mocks",
    ],
)
