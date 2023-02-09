load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)
load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "LEGACY_ALWAYSLINK",
)

licenses(["notice"])  # Apache 2

# Extensions of QUIC server's preferred address.

envoy_extension_package()

envoy_cc_library(
    name = "basic_server_preferred_address_config_lib",
    srcs = ["basic_server_preferred_address_config.cc"],
    hdrs = ["basic_server_preferred_address_config.h"],
    tags = ["nofips"],
    deps = [
        "//envoy/registry",
        "//source/common/quic:envoy_quic_server_preferred_address_config_factory_interface",
        "@envoy_api//envoy/extensions/quic/server_preferred_address/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_extension(
    name = "basic_server_preferred_address_config_factory_config",
    extra_visibility = [
        "//test:__subpackages__",
    ],
    tags = ["nofips"],
    deps = select(
        {
            "//bazel:boringssl_fips": [],
            "//bazel:boringssl_disabled": [],
            "//conditions:default": [
                ":basic_server_preferred_address_config_lib",
            ],
        },
    ),
)
