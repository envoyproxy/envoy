load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "sxg_lib",
    srcs = [
        "encoder.cc",
        "filter.cc",
    ],
    hdrs = [
        "encoder.h",
        "filter.h",
    ],
    external_deps = ["libsxg"],
    deps = [
        "//envoy/server:filter_config_interface",
        "//source/common/config:datasource_lib",
        "//source/common/http:codes_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@boringssl//:ssl",
        "@envoy_api//envoy/extensions/filters/http/sxg/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":sxg_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/sxg/v3alpha:pkg_cc_proto",
    ],
)
