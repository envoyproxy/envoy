licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "envoy_quic_alarm_lib",
    srcs = ["envoy_quic_alarm.cc"],
    hdrs = ["envoy_quic_alarm.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "@com_googlesource_quiche//:quic_core_alarm_interface_lib",
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
        "@com_googlesource_quiche//:quic_core_alarm_factory_interface_lib",
        "@com_googlesource_quiche//:quic_core_arena_scoped_ptr_lib",
        "@com_googlesource_quiche//:quic_core_one_block_arena_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_packet_writer_lib",
    srcs = ["envoy_quic_packet_writer.cc"],
    hdrs = ["envoy_quic_packet_writer.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_utils_lib",
        "@com_googlesource_quiche//:quic_core_packet_writer_interface_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_proof_source_lib",
    hdrs = ["envoy_quic_fake_proof_source.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        "@com_googlesource_quiche//:quic_core_crypto_proof_source_interface_lib",
        "@com_googlesource_quiche//:quic_core_versions_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_proof_verifier_lib",
    hdrs = ["envoy_quic_fake_proof_verifier.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        "@com_googlesource_quiche//:quic_core_crypto_crypto_handshake_lib",
        "@com_googlesource_quiche//:quic_core_versions_lib",
    ],
)

envoy_cc_library(
    name = "spdy_server_push_utils_for_envoy_lib",
    srcs = ["spdy_server_push_utils_for_envoy.cc"],
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/common:assert_lib",
        "@com_googlesource_quiche//:quic_core_http_spdy_server_push_utils_header",
    ],
)

envoy_cc_library(
    name = "envoy_quic_utils_lib",
    hdrs = ["envoy_quic_utils.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = ["//source/common/network:address_lib"],
)
