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
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_name = "envoy.filters.http.bandwidth_limit",
    deps = [
        "//source/extensions/filters/http/bandwidth_limit:bandwidth_limit_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/http:http_mocks",
        "@envoy_api//envoy/extensions/filters/http/bandwidth_limit/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.bandwidth_limit",
    deps = [
        "//source/extensions/filters/http/bandwidth_limit:config",
        "//test/mocks/server:server_mocks",
    ],
)
