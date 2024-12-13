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

envoy_extension_package()

envoy_cc_library(
    name = "quic_stats_lib",
    srcs = ["quic_stats.cc"],
    hdrs = ["quic_stats.h"],
    tags = ["nofips"],
    visibility = [
        "//test:__subpackages__",
    ],
    deps = [
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/common/quic:envoy_quic_connection_debug_visitor_factory_interface",
        "@envoy_api//envoy/extensions/quic/connection_debug_visitor/quic_stats/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_extension(
    name = "config",
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
                ":quic_stats_lib",
            ],
        },
    ),
)
