load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Extensions of various QUIC objects.

envoy_extension_package()

envoy_cc_extension(
    name = "envoy_quic_crypto_server_stream_lib",
    srcs = ["envoy_quic_crypto_server_stream.cc"],
    hdrs = ["envoy_quic_crypto_server_stream.h"],
    category = "quic.server.crypto_stream",
    security_posture = "unknown",
    deps = [
        "//source/common/quic:envoy_quic_crypto_stream_factory_lib",
        "@envoy_api//envoy/extensions/quic/v3:pkg_cc_proto",
    ],
)
