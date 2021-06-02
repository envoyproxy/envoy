load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# Extensions of various QUIC objects.

envoy_package()

envoy_cc_library(
    name = "envoy_quic_crypto_server_stream_lib",
    srcs = ["envoy_quic_crypto_server_stream.cc"],
    hdrs = ["envoy_quic_crypto_server_stream.h"],
    visibility = [
        "//source/common/quic:__subpackages__",
        "//test:__subpackages__",
    ],
    tags = ["nofips"],
    deps = [
        "//source/common/quic:envoy_quic_crypto_stream_factory_lib",
        "@envoy_api//envoy/extensions/quic/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "envoy_quic_crypto_client_stream_lib",
    srcs = ["envoy_quic_crypto_client_stream.cc"],
    hdrs = ["envoy_quic_crypto_client_stream.h"],
    tags = ["nofips"],
    visibility = [
        "//source/common/quic:__subpackages__",
        "//test:__subpackages__",
    ],
    deps = [
        "//source/common/quic:envoy_quic_crypto_stream_factory_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_proof_source_factory_impl_lib",
    srcs = ["envoy_quic_proof_source_factory_impl.cc"],
    hdrs = ["envoy_quic_proof_source_factory_impl.h"],
    visibility = [
        "//source/common/quic:__subpackages__",
        "//test:__subpackages__",
    ],
    tags = ["nofips"],
    deps = [
        "//source/common/quic:envoy_quic_proof_source_factory_interface",
        "//source/common/quic:envoy_quic_proof_source_lib",
        "@envoy_api//envoy/extensions/quic/v3:pkg_cc_proto",
    ],
)
