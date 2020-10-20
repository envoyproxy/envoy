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
    extension_name = "envoy.filters.http.local_ratelimit",
    deps = [
        "//source/extensions/filters/http/local_ratelimit:local_ratelimit_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/http:http_mocks",
        "@envoy_api//envoy/extensions/filters/http/local_ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.local_ratelimit",
    deps = [
        "//source/extensions/filters/http/local_ratelimit:config",
        "//test/mocks/server:server_mocks",
    ],
)
