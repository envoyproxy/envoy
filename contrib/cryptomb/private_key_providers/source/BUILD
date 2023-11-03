load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_cmake",
    "envoy_contrib_package",
)
load(
    "//contrib:all_contrib_extensions.bzl",
    "envoy_contrib_linux_x86_64_constraints",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cmake(
    name = "ipp-crypto",
    cache_entries = {
        "BORINGSSL": "on",
    },
    defines = [
        "OPENSSL_USE_STATIC_LIBS=TRUE",
    ],
    lib_source = select({
        "//bazel:boringssl_fips": "@com_github_intel_ipp_crypto_crypto_mb_fips//:all",
        "//conditions:default": "@com_github_intel_ipp_crypto_crypto_mb//:all",
    }),
    out_static_libs = ["libcrypto_mb.a"],
    tags = ["skip_on_windows"],
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    visibility = ["//visibility:private"],
    working_directory = "sources/ippcp/crypto_mb",
    # Use boringssl alias to select fips vs non-fips version.
    deps = ["//bazel:boringssl"],
)

envoy_cc_library(
    name = "ipp_crypto_wrapper_lib",
    hdrs = ["ipp_crypto.h"] + select({
        "//bazel:linux_x86_64": [
            "ipp_crypto_impl.h",
        ],
        "//conditions:default": [
        ],
    }),
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
            ":ipp-crypto",
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
    visibility = ["//visibility:public"],
    deps = [
        ":cryptomb_stats_lib",
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
        "@envoy_api//contrib/envoy/extensions/private_key_providers/cryptomb/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "cryptomb_stats_lib",
    srcs = [
        "cryptomb_stats.cc",
    ],
    hdrs = [
        "cryptomb_stats.h",
    ],
    deps = [
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    defines = select({
        "//bazel:linux_x86_64": [],
        "//conditions:default": [
            "IPP_CRYPTO_DISABLED=1",
        ],
    }),
    deps = [
        "//envoy/registry",
        "//envoy/ssl/private_key:private_key_config_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/private_key_providers/cryptomb/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ] + select({
        "//bazel:linux_x86_64": [
            ":cryptomb_private_key_provider_lib",
            ":ipp_crypto_wrapper_lib",
        ],
        "//conditions:default": [
        ],
    }),
)
