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
        "//test/test_common:utility_lib",
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
