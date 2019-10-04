licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.alpn",
    deps = [
        "//source/extensions/filters/http/alpn:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/filter/http/alpn/v2alpha:alpn_cc",
    ],
)

envoy_extension_cc_test(
    name = "alpn_filter_test",
    srcs = ["alpn_filter_test.cc"],
    extension_name = "envoy.filters.http.alpn",
    deps = [
        "//source/extensions/filters/http/alpn:alpn_filter_lib",
        "//test/mocks:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/filter/http/alpn/v2alpha:alpn_cc",
    ],
)
