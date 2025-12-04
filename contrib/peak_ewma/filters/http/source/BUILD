load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "peak_ewma_http_filter_lib",
    srcs = [
        "peak_ewma_filter.cc",
    ],
    hdrs = [
        "peak_ewma_filter.h",
    ],
    repository = "@envoy",
    deps = [
        "//contrib/peak_ewma/load_balancing_policies/source:peak_ewma_lb_lib",
        "//envoy/http:filter_interface",
        "//source/common/common:utility_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["peak_ewma_filter_config.cc"],
    hdrs = ["peak_ewma_filter_config.h"],
    deps = [
        ":peak_ewma_http_filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/peak_ewma/v3alpha:pkg_cc_proto",
    ],
)
