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
    srcs = ["quic_lb.cc"],
    hdrs = ["quic_lb.h"],
    tags = ["nofips"],
    deps = [
        "//source/common/config:datasource_lib",
        "//source/common/quic:envoy_quic_connection_id_generator_factory_interface",
        "//source/common/quic:envoy_quic_utils_lib",
        "@com_github_google_quiche//:quic_load_balancer_config_lib",
        "@com_github_google_quiche//:quic_load_balancer_encoder_lib",
        "@com_github_google_quiche//:quic_load_balancer_server_id_lib",
        "@envoy_api//envoy/extensions/quic/connection_id_generator/quic_lb/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    tags = ["nofips"],
    deps = [
        ":quic_lb_lib",
        "//envoy/registry",
        "//source/common/quic:envoy_quic_connection_id_generator_factory_interface",
        "@envoy_api//envoy/extensions/quic/connection_id_generator/quic_lb/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_extension(
    name = "quic_lb_config",
    tags = ["nofips"],
    deps = select(
        {
            "//bazel:boringssl_fips": [],
            "//bazel:boringssl_disabled": [],
            "//conditions:default": [
                ":config_lib",
            ],
        },
    ),
)
