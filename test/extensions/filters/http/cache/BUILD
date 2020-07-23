load("//bazel:envoy_build_system.bzl", "envoy_package")
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "cache_headers_utils_test",
    srcs = ["cache_headers_utils_test.cc"],
    extension_name = "envoy.filters.http.cache",
    deps = [
        "//include/envoy/http:header_map_interface",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/cache:cache_headers_utils_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "http_cache_test",
    srcs = ["http_cache_test.cc"],
    extension_name = "envoy.filters.http.cache",
    deps = [
        "//source/extensions/filters/http/cache:http_cache_lib",
        "//source/extensions/filters/http/cache/simple_http_cache:simple_http_cache_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "cache_filter_test",
    srcs = ["cache_filter_test.cc"],
    extension_name = "envoy.filters.http.cache",
    deps = [
        "//source/extensions/filters/http/cache:cache_filter_lib",
        "//source/extensions/filters/http/cache/simple_http_cache:simple_http_cache_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "cacheability_utils_test",
    srcs = ["cacheability_utils_test.cc"],
    extension_name = "envoy.filters.http.cache",
    deps = [
        "//source/extensions/filters/http/cache:cacheability_utils_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.cache",
    deps = [
        "//source/extensions/filters/http/cache:config",
        "//source/extensions/filters/http/cache/simple_http_cache:simple_http_cache_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "cache_filter_integration_test",
    srcs = [
        "cache_filter_integration_test.cc",
    ],
    extension_name = "envoy.filters.http.cache",
    tags = ["fails_on_windows"],
    deps = [
        "//source/extensions/filters/http/cache:config",
        "//source/extensions/filters/http/cache:http_cache_lib",
        "//source/extensions/filters/http/cache/simple_http_cache:simple_http_cache_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)
