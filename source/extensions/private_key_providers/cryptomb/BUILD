load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "ipp_crypto_wrapper_lib",
    hdrs = [
        "ipp_crypto.h",
        "ipp_crypto_disabled_impl.h",
        "ipp_crypto_impl.h",
    ],
    defines = select({
        "//bazel:linux_x86_64": [],
        "//conditions:default": [
            "IPP_CRYPTO_DISABLED=1",
        ],
    }),
    external_deps = ["ssl"],
    repository = "@envoy",
    deps = select({
        "//bazel:linux_x86_64": [
            "//bazel/foreign_cc:ipp-crypto",
        ],
        "//conditions:default": [],
    }),
)

envoy_cc_library(
    name = "cryptomb_private_key_provider_lib",
    srcs = [
        "cryptomb_private_key_provider.cc",
    ],
    hdrs = [
        "cryptomb_private_key_provider.h",
    ],
    external_deps = ["ssl"],
    repository = "@envoy",
    deps = [
        ":ipp_crypto_wrapper_lib",
        "//envoy/api:api_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/ssl/private_key:private_key_config_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/config:datasource_lib",
        "@envoy_api//envoy/extensions/private_key_providers/cryptomb/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":cryptomb_private_key_provider_lib",
        "//envoy/registry",
        "//envoy/ssl/private_key:private_key_config_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/private_key_providers/cryptomb/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
