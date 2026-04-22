load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "alpn_filter",
    srcs = ["alpn_filter.cc"],
    hdrs = ["alpn_filter.h"],
    repository = "@envoy",
    deps = [
        "//envoy/http:filter_interface",
        "//source/common/network:application_protocol_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/alpn/v3:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":alpn_filter",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/alpn/v3:pkg_cc_proto",
    ],
)
