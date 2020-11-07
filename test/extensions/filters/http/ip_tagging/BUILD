load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "ip_tagging_filter_test",
    srcs = ["ip_tagging_filter_test.cc"],
    extension_name = "envoy.filters.http.ip_tagging",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/filters/http/ip_tagging:config",
        "//source/extensions/filters/http/ip_tagging:ip_tagging_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/ip_tagging/v3:pkg_cc_proto",
    ],
)
