load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# HTTP L7 filter that writes an IP tagging header based on IP trie data
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/ip_tagging_filter

envoy_extension_package()

envoy_cc_library(
    name = "ip_tagging_filter_lib",
    srcs = ["ip_tagging_filter.cc"],
    hdrs = ["ip_tagging_filter.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/runtime:runtime_interface",
        "//source/common/common:assert_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/network:lc_trie_lib",
        "//source/common/stats:symbol_table_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/ip_tagging/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/ip_tagging:ip_tagging_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/ip_tagging/v3:pkg_cc_proto",
    ],
)
