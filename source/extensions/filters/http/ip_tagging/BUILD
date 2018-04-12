licenses(["notice"])  # Apache 2
# HTTP L7 filter that writes an IP tagging header based on IP trie data
# Public docs: https://github.com/envoyproxy/data-plane-api/blob/master/docs/root/configuration/http_filters/ip_tagging_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "ip_tagging_filter_lib",
    srcs = ["ip_tagging_filter.cc"],
    hdrs = ["ip_tagging_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/runtime:runtime_interface",
        "//source/common/common:assert_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:lc_trie_lib",
        "@envoy_api//envoy/config/filter/http/ip_tagging/v2:ip_tagging_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/ip_tagging:ip_tagging_filter_lib",
        "@envoy_api//envoy/config/filter/http/ip_tagging/v2:ip_tagging_cc",
    ],
)
