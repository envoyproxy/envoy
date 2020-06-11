load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "envoy_quic_alarm_test",
    srcs = ["envoy_quic_alarm_test.cc"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        "//source/extensions/quic_listeners/quiche:envoy_quic_alarm_factory_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_alarm_lib",
        "//source/extensions/quic_listeners/quiche/platform:envoy_quic_clock_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_writer_test",
    srcs = ["envoy_quic_writer_test.cc"],
    external_deps = ["quiche_quic_platform"],
    # Skipping as quiche quic_stream_send_buffer.cc does not currently compile on Windows
    tags = [
        "nofips",
        "skip_on_windows",
    ],
    deps = [
        "//source/common/network:io_socket_error_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_packet_writer_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_proof_source_test",
    srcs = ["envoy_quic_proof_source_test.cc"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        "//source/extensions/quic_listeners/quiche:envoy_quic_proof_source_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_proof_verifier_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "@com_googlesource_quiche//:quic_core_versions_lib",
        "@com_googlesource_quiche//:quic_test_tools_test_certificates_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_server_stream_test",
    srcs = ["envoy_quic_server_stream_test.cc"],
    # Skipping as quiche quic_stream_send_buffer.cc does not currently compile on Windows
    tags = [
        "nofips",
        "skip_on_windows",
    ],
    deps = [
        ":quic_test_utils_for_envoy_lib",
        ":test_utils_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_alarm_factory_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_connection_helper_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_server_connection_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_server_session_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:stream_decoder_mock",
        "//test/mocks/network:network_mocks",
        "//test/test_common:utility_lib",
        "@com_googlesource_quiche//:quic_core_http_spdy_session_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_client_stream_test",
    srcs = ["envoy_quic_client_stream_test.cc"],
    # Skipping as quiche quic_stream_send_buffer.cc does not currently compile on Windows
    tags = [
        "nofips",
        "skip_on_windows",
    ],
    deps = [
        ":quic_test_utils_for_envoy_lib",
        ":test_utils_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_alarm_factory_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_client_connection_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_client_session_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_connection_helper_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:stream_decoder_mock",
        "//test/mocks/network:network_mocks",
        "//test/test_common:utility_lib",
        "@com_googlesource_quiche//:quic_core_http_spdy_session_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_server_session_test",
    srcs = ["envoy_quic_server_session_test.cc"],
    # Skipping as quiche quic_stream_send_buffer.cc does not currently compile on Windows
    tags = [
        "nofips",
        "skip_on_windows",
    ],
    deps = [
        ":quic_test_utils_for_envoy_lib",
        ":test_proof_source_lib",
        ":test_utils_lib",
        "//include/envoy/stats:stats_macros",
        "//source/extensions/quic_listeners/quiche:codec_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_alarm_factory_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_connection_helper_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_server_connection_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_server_session_lib",
        "//source/server:configuration_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:stream_decoder_mock",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:global_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:simulated_time_system_lib",
        "@com_googlesource_quiche//:quic_test_tools_config_peer_lib",
        "@com_googlesource_quiche//:quic_test_tools_server_session_base_peer",
        "@com_googlesource_quiche//:quic_test_tools_test_utils_interface_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_client_session_test",
    srcs = ["envoy_quic_client_session_test.cc"],
    # Skipping as quiche quic_stream_send_buffer.cc does not currently compile on Windows
    tags = [
        "nofips",
        "skip_on_windows",
    ],
    deps = [
        ":quic_test_utils_for_envoy_lib",
        ":test_utils_lib",
        "//include/envoy/stats:stats_macros",
        "//source/extensions/quic_listeners/quiche:codec_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_alarm_factory_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_client_connection_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_client_session_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_connection_helper_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:stream_decoder_mock",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_cc_test(
    name = "active_quic_listener_test",
    srcs = ["active_quic_listener_test.cc"],
    # Skipping as quiche quic_stream_send_buffer.cc does not currently compile on Windows
    tags = [
        "nofips",
        "skip_on_windows",
    ],
    deps = [
        ":quic_test_utils_for_envoy_lib",
        ":test_utils_lib",
        "//source/extensions/quic_listeners/quiche:active_quic_listener_config_lib",
        "//source/extensions/quic_listeners/quiche:active_quic_listener_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_utils_lib",
        "//source/server:configuration_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:network_utility_lib",
        "//test/test_common:simulated_time_system_lib",
        "@com_googlesource_quiche//:quic_test_tools_crypto_server_config_peer_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "envoy_quic_dispatcher_test",
    srcs = ["envoy_quic_dispatcher_test.cc"],
    # quic_sent_packet_manager.cc does not compile error: warning C4715: 'quic::QuicSentPacketManager::OnRetransmissionTimeout': not all control paths return a value
    tags = [
        "nofips",
        "skip_on_windows",
    ],
    deps = [
        ":quic_test_utils_for_envoy_lib",
        ":test_proof_source_lib",
        ":test_utils_lib",
        "//include/envoy/stats:stats_macros",
        "//source/extensions/quic_listeners/quiche:envoy_quic_alarm_factory_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_connection_helper_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_dispatcher_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_proof_source_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_server_session_lib",
        "//source/server:configuration_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:global_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_cc_test_library(
    name = "test_proof_source_lib",
    hdrs = ["test_proof_source.h"],
    tags = ["nofips"],
    deps = [
        "//source/extensions/quic_listeners/quiche:envoy_quic_fake_proof_source_lib",
        "@com_googlesource_quiche//:quic_test_tools_test_certificates_lib",
    ],
)

envoy_cc_test_library(
    name = "quic_test_utils_for_envoy_lib",
    srcs = ["crypto_test_utils_for_envoy.cc"],
    tags = ["nofips"],
    deps = [
        ":test_proof_source_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_proof_verifier_lib",
        "@com_googlesource_quiche//:quic_test_tools_test_utils_interface_lib",
    ],
)

envoy_cc_test(
    name = "quic_io_handle_wrapper_test",
    srcs = ["quic_io_handle_wrapper_test.cc"],
    tags = ["nofips"],
    deps = [
        "//source/extensions/quic_listeners/quiche:quic_io_handle_wrapper_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_utils_test",
    srcs = ["envoy_quic_utils_test.cc"],
    # Skipping as quiche quic_stream_send_buffer.cc does not currently compile on Windows
    tags = [
        "nofips",
        "skip_on_windows",
    ],
    deps = [
        ":quic_test_utils_for_envoy_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_utils_lib",
        "//test/mocks/api:api_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test(
    name = "active_quic_listener_config_test",
    srcs = ["active_quic_listener_config_test.cc"],
    # Skipping as quiche quic_stream_send_buffer.cc does not currently compile on Windows
    tags = [
        "nofips",
        "skip_on_windows",
    ],
    deps = [
        "//source/common/config:utility_lib",
        "//source/extensions/quic_listeners/quiche:active_quic_listener_config_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_simulated_watermark_buffer_test",
    srcs = ["envoy_quic_simulated_watermark_buffer_test.cc"],
    # Skipping as quiche quic_stream_send_buffer.cc does not currently compile on Windows
    tags = [
        "nofips",
        "skip_on_windows",
    ],
    deps = ["//source/extensions/quic_listeners/quiche:envoy_quic_simulated_watermark_buffer_lib"],
)

envoy_cc_test_library(
    name = "test_utils_lib",
    hdrs = ["test_utils.h"],
    external_deps = ["bazel_runfiles"],
    tags = ["nofips"],
    deps = [
        "//source/extensions/quic_listeners/quiche:quic_filter_manager_connection_lib",
        "//test/test_common:environment_lib",
        "@com_googlesource_quiche//:quic_core_http_spdy_session_lib",
        "@com_googlesource_quiche//:quic_test_tools_first_flight_lib",
    ],
)
