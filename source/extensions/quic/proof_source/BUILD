load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "LEGACY_ALWAYSLINK",
)
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Extensions of QUIC proof source.

envoy_extension_package()

envoy_cc_library(
    name = "envoy_quic_proof_source_factory_impl_lib",
    srcs = ["envoy_quic_proof_source_factory_impl.cc"],
    hdrs = ["envoy_quic_proof_source_factory_impl.h"],
    tags = ["nofips"],
    visibility = [
        "//source/common/quic:__subpackages__",
        "//test:__subpackages__",
    ],
    deps = [
        "//source/common/quic:envoy_quic_proof_source_factory_interface",
        "//source/common/quic:envoy_quic_proof_source_lib",
        "@envoy_api//envoy/extensions/quic/proof_source/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_extension(
    name = "envoy_quic_default_proof_source",
    extra_visibility = [
        "//source/common/quic:__subpackages__",
        "//test:__subpackages__",
    ],
    tags = ["nofips"],
    deps = select(
        {
            "//bazel:boringssl_fips": [],
            "//bazel:boringssl_disabled": [],
            "//conditions:default": [
                ":envoy_quic_proof_source_factory_impl_lib",
            ],
        },
    ),
)
