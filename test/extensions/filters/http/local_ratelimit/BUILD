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
    extension_names = ["envoy.filters.http.local_ratelimit"],
    rbe_pool = "2core",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/singleton:manager_impl_lib",
        "//source/extensions/filters/http/local_ratelimit:local_ratelimit_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/local_ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.local_ratelimit"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/local_ratelimit:config",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "local_ratelimit_integration_test",
    size = "large",
    srcs = ["local_ratelimit_integration_test.cc"],
    extension_names = ["envoy.filters.http.local_ratelimit"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        "//source/extensions/filters/http/local_ratelimit:config",
        "//test/integration:http_protocol_integration_lib",
    ],
)
