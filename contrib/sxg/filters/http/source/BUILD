load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "sxg_lib",
    srcs = [
        "encoder.cc",
        "filter.cc",
        "filter_config.cc",
    ],
    hdrs = [
        "encoder.h",
        "filter.h",
        "filter_config.h",
    ],
    deps = [
        "//envoy/server:filter_config_interface",
        "//source/common/config:datasource_lib",
        "//source/common/http:codes_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
        "//source/common/secret:secret_provider_impl_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/sxg/v3alpha:pkg_cc_proto",
        "//bazel/foreign_cc:libsxg",
        # use boringssl alias to select fips vs non-fips version.
        "//bazel:boringssl",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":sxg_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/sxg/v3alpha:pkg_cc_proto",
    ],
)
