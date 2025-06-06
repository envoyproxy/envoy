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

# Extensions of QUIC connection ID generator.

envoy_extension_package()

envoy_cc_library(
    name = "envoy_deterministic_connection_id_generator_config_lib",
    srcs = select({
        "//bazel:disable_http3": [],
        "//conditions:default": ["envoy_deterministic_connection_id_generator_config.cc"],
    }),
    hdrs = select({
        "//bazel:disable_http3": [],
        "//conditions:default": ["envoy_deterministic_connection_id_generator_config.h"],
    }),
    deps = select({
        "//bazel:disable_http3": [],
        "//conditions:default": [
            "//envoy/registry",
            "//source/common/quic:envoy_deterministic_connection_id_generator_lib",
            "//source/common/quic:envoy_quic_connection_id_generator_factory_interface",
            "@envoy_api//envoy/extensions/quic/connection_id_generator/v3:pkg_cc_proto",
        ],
    }),
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_extension(
    name = "envoy_deterministic_connection_id_generator_config",
    extra_visibility = [
        "//source/common/quic:__subpackages__",
    ],
    deps = select({
        "//bazel:disable_http3": [],
        "//conditions:default": [
            ":envoy_deterministic_connection_id_generator_config_lib",
        ],
    }),
)
