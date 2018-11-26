licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test_library(
    name = "add_trailers_filter_config_lib",
    srcs = [
        "add_trailers_filter.cc",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:empty_http_filter_config_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_test_library(
    name = "passthrough_filter_config_lib",
    srcs = [
        "passthrough_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:empty_http_filter_config_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_test_library(
    name = "headers_only_filter_config_lib",
    srcs = [
        "headers_only_filter.cc",
    ],
    deps = [
        ":common_lib",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:empty_http_filter_config_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_test_library(
    name = "pause_filter_lib",
    srcs = [
        "pause_filter.cc",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//source/common/network:connection_lib",
        "//source/extensions/filters/http/common:empty_http_filter_config_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_test_library(
    name = "common_lib",
    hdrs = [
        "common.h",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//source/extensions/filters/http/common:empty_http_filter_config_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "random_pause_filter_lib",
    srcs = [
        "random_pause_filter.cc",
    ],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//source/common/network:connection_lib",
        "//source/extensions/filters/http/common:empty_http_filter_config_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//test/test_common:utility_lib",
    ],
)
