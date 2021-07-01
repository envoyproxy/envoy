load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# SNI validation L7 HTTP filter
# Public docs: docs/root/configuration/http_filters/sni_validation.rst

envoy_extension_package()

envoy_cc_library(
    name = "sni_validation_lib",
    srcs = ["sni_validation.cc"],
    hdrs = ["sni_validation.h"],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:utility_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/router:header_parser_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_lib",
        "//source/extensions/filters/common/sni_validation:sni_validation_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    hdrs = ["config.h"],
    deps = [
        ":sni_validation_lib",
        "//envoy/http:filter_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/san_validation/v3:pkg_cc_proto",
    ],
)
