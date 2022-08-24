load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

# Language detection filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/language_filter

envoy_contrib_package()

envoy_cc_contrib_extension(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    tags = ["skip_on_windows"],
    deps = [
        ":filter_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/language/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["language_filter.cc"],
    hdrs = ["language_filter.h"],
    tags = ["skip_on_windows"],
    deps = [
        "//bazel/foreign_cc:unicode_icu",
        "//source/common/common:empty_string",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:utility_lib",
    ],
)
