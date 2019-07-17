licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

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
    ],
)

envoy_cc_test(
    name = "envoy_quic_writer_test",
    srcs = ["envoy_quic_writer_test.cc"],
    external_deps = ["quiche_quic_platform"],
    deps = [
        "//source/common/network:io_socket_error_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_packet_writer_lib",
        "//test/mocks/network:network_mocks",
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
        "@com_googlesource_quiche//:quic_core_versions_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_server_stream_test",
    srcs = ["envoy_quic_server_stream_test.cc"],
    tags = ["nofips"],
    deps = [
        ":quic_test_utils_for_envoy_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_alarm_factory_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_connection_helper_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_connection_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_server_stream_lib",
        "//test/mocks/http:stream_decoder_mock",
        "//test/test_common:simulated_time_system_lib",
        "@com_googlesource_quiche//:quic_core_http_spdy_session_lib",
    ],
)

envoy_cc_test(
    name = "envoy_quic_server_session_test",
    srcs = ["envoy_quic_server_session_test.cc"],
    tags = ["nofips"],
    deps = [
        ":quic_test_utils_for_envoy_lib",
        "//include/envoy/stats:stats_macros",
        "//source/extensions/quic_listeners/quiche:codec_impl_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_alarm_factory_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_connection_helper_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_server_session_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:stream_decoder_mock",
        "//test/mocks/network:connection_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:global_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_cc_test_library(
    name = "quic_test_utils_for_envoy_lib",
    srcs = ["crypto_test_utils_for_envoy.cc"],
    tags = ["nofips"],
    deps = [
        "//source/extensions/quic_listeners/quiche:envoy_quic_proof_source_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_proof_verifier_lib",
        "@com_googlesource_quiche//:quic_test_tools_test_utils_interface_lib",
    ],
)
