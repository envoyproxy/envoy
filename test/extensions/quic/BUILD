load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# Test-only extensions of various QUIC objects.

envoy_extension_package()

envoy_cc_extension(
    name = "envoy_quic_no_handshake_crypto_server_stream_lib",
    srcs = ["envoy_quic_no_handshake_crypto_server_stream.cc"],
    hdrs = ["envoy_quic_no_handshake_crypto_server_stream.h"],
    category = "envoy.quic.server.crypto_stream",
    security_posture = "unknown",
    tags = ["nofips"],
    deps = [
        "//source/common/quic:envoy_quic_crypto_stream_factory_lib",
        "@com_googlesource_quiche//:quic_core_crypto_encryption_lib",
        "@com_googlesource_quiche//:quic_core_session_lib",
    ],
)
