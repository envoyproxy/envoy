load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Request body size limiter L7 HTTP filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/body_size_limit_filter

envoy_extension_package()

envoy_cc_library(
    name = "body_size_limit_filter_lib",
    srcs = ["body_size_limit_filter.cc"],
    hdrs = ["body_size_limit_filter.h"],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "@envoy_api//envoy/extensions/filters/http/body_size_limit/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/extensions/filters/http/body_size_limit:body_size_limit_filter_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/body_size_limit/v3:pkg_cc_proto",
    ],
)
