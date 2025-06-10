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
    name = "quic_lb_lib",
    srcs = select({
        "//bazel:disable_http3": [],
        "//conditions:default": ["quic_lb.cc"],
    }),
    hdrs = select({
        "//bazel:disable_http3": [],
        "//conditions:default": ["quic_lb.h"],
    }),
    deps = select({
        "//bazel:disable_http3": [],
        "//conditions:default": [
            "//source/common/config:datasource_lib",
            "//source/common/quic:envoy_quic_connection_id_generator_factory_interface",
            "//source/common/quic:envoy_quic_utils_lib",
            "@com_github_google_quiche//:quic_load_balancer_config_lib",
            "@com_github_google_quiche//:quic_load_balancer_encoder_lib",
            "@com_github_google_quiche//:quic_load_balancer_server_id_lib",
            "@envoy_api//envoy/extensions/quic/connection_id_generator/quic_lb/v3:pkg_cc_proto",
        ],
    }),
)

envoy_cc_library(
    name = "config_lib",
    srcs = select({
        "//bazel:disable_http3": [],
        "//conditions:default": ["config.cc"],
    }),
    hdrs = select({
        "//bazel:disable_http3": [],
        "//conditions:default": ["config.h"],
    }),
    deps = select({
        "//bazel:disable_http3": [],
        "//conditions:default": [
            ":quic_lb_lib",
            "//envoy/registry",
            "//source/common/quic:envoy_quic_connection_id_generator_factory_interface",
            "@envoy_api//envoy/extensions/quic/connection_id_generator/quic_lb/v3:pkg_cc_proto",
        ],
    }),
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_extension(
    name = "quic_lb_config",
    deps = select({
        "//bazel:disable_http3": [],
        "//conditions:default": [
            ":config_lib",
        ],
    }),
)
