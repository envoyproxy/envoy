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
    name = "envoy_deterministic_connection_id_generator_lib",
    srcs = ["envoy_deterministic_connection_id_generator.cc"],
    hdrs = ["envoy_deterministic_connection_id_generator.h"],
    tags = ["nofips"],
    deps = [
        "//source/common/quic:envoy_quic_connection_id_generator_factory_interface",
        "//source/common/quic:envoy_quic_utils_lib",
        "@com_github_google_quiche//:quic_core_deterministic_connection_id_generator_lib",
    ],
)

envoy_cc_library(
    name = "envoy_deterministic_connection_id_generator_config_lib",
    srcs = ["envoy_deterministic_connection_id_generator_config.cc"],
    hdrs = ["envoy_deterministic_connection_id_generator_config.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_deterministic_connection_id_generator_lib",
        "//envoy/registry",
        "//source/common/quic:envoy_quic_connection_id_generator_factory_interface",
        "@envoy_api//envoy/extensions/quic/connection_id_generator/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_extension(
    name = "envoy_deterministic_connection_id_generator_config",
    extra_visibility = [
        "//source/common/quic:__subpackages__",
    ],
    tags = ["nofips"],
    deps = select(
        {
            "//bazel:boringssl_fips": [],
            "//bazel:boringssl_disabled": [],
            "//conditions:default": [
                ":envoy_deterministic_connection_id_generator_config_lib",
            ],
        },
    ),
)
