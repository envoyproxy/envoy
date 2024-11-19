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

# Extensions of QUIC connection debug visitor.

envoy_extension_package()

envoy_cc_library(
    name = "envoy_quic_connection_debug_visitor_basic_lib",
    srcs = ["envoy_quic_connection_debug_visitor_basic.cc"],
    hdrs = ["envoy_quic_connection_debug_visitor_basic.h"],
    tags = ["nofips"],
    visibility = [
        "//source/common/quic:__subpackages__",
    ],
    deps = [
        "//envoy/registry",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/quic:envoy_quic_connection_debug_visitor_factory_interface",
        "@com_github_google_quiche//:quic_core_connection_lib",
        "@com_github_google_quiche//:quic_core_frames_frames_lib",
        "@com_github_google_quiche//:quic_core_session_lib",
        "@com_github_google_quiche//:quic_core_types_lib",
        "@envoy_api//envoy/extensions/quic/connection_debug_visitor/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_extension(
    name = "envoy_quic_connection_debug_visitor_basic",
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
                ":envoy_quic_connection_debug_visitor_basic_lib",
            ],
        },
    ),
)
