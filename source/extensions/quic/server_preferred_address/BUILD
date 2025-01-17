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

# Extensions of QUIC server's preferred address.

envoy_extension_package()

envoy_cc_library(
    name = "server_preferred_address_lib",
    srcs = ["server_preferred_address.cc"],
    hdrs = ["server_preferred_address.h"],
    tags = ["nofips"],
    deps = [
        "//source/common/quic:envoy_quic_server_preferred_address_config_factory_interface",
    ],
)

envoy_cc_library(
    name = "fixed_server_preferred_address_config_lib",
    srcs = ["fixed_server_preferred_address_config.cc"],
    hdrs = ["fixed_server_preferred_address_config.h"],
    tags = ["nofips"],
    deps = [
        ":server_preferred_address_lib",
        "//envoy/registry",
        "//source/common/quic:envoy_quic_server_preferred_address_config_factory_interface",
        "//source/common/quic:envoy_quic_utils_lib",
        "@envoy_api//envoy/extensions/quic/server_preferred_address/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_extension(
    name = "fixed_server_preferred_address_config_factory_config",
    extra_visibility = [
        "//test:__subpackages__",
    ],
    tags = ["nofips"],
    deps = select(
        {
            "//bazel:boringssl_fips": [],
            "//bazel:boringssl_disabled": [],
            "//conditions:default": [
                ":fixed_server_preferred_address_config_lib",
            ],
        },
    ),
)

envoy_cc_library(
    name = "datasource_server_preferred_address_config_lib",
    srcs = ["datasource_server_preferred_address_config.cc"],
    hdrs = ["datasource_server_preferred_address_config.h"],
    tags = ["nofips"],
    deps = [
        ":server_preferred_address_lib",
        "//envoy/registry",
        "//source/common/config:datasource_lib",
        "//source/common/quic:envoy_quic_server_preferred_address_config_factory_interface",
        "//source/common/quic:envoy_quic_utils_lib",
        "@envoy_api//envoy/extensions/quic/server_preferred_address/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_extension(
    name = "datasource_server_preferred_address_config_factory_config",
    tags = ["nofips"],
    deps = select(
        {
            "//bazel:boringssl_fips": [],
            "//bazel:boringssl_disabled": [],
            "//conditions:default": [
                ":datasource_server_preferred_address_config_lib",
            ],
        },
    ),
)
