load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "mock_lib",
    srcs = [
        "mock.cc",
    ],
    hdrs = [
        "mock.h",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/common:jwks_fetcher_lib",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "empty_http_filter_config_lib",
    hdrs = ["empty_http_filter_config.h"],
    deps = [
        "//envoy/server:filter_config_interface",
    ],
)

envoy_extension_cc_test(
    name = "jwks_fetcher_test",
    srcs = [
        "jwks_fetcher_test.cc",
    ],
    extension_names = ["envoy.filters.http.jwt_authn"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/common:jwks_fetcher_lib",
        "//test/extensions/filters/http/common:mock_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "stream_rate_limiter_test",
    srcs = ["stream_rate_limiter_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/event:dispatcher_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/http/common:stream_rate_limiter_lib",
        "//test/common/http:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)
