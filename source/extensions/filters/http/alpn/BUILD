licenses(["notice"])  # Apache 2

# A filter for overriding application protocols.

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":alpn_filter_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
    ],
)

envoy_cc_library(
    name = "alpn_filter_lib",
    srcs = ["alpn_filter.cc"],
    hdrs = ["alpn_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//source/common/network:application_protocol_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/config/filter/http/alpn/v2alpha:alpn_cc",
    ],
)
