load("//bazel:envoy_build_system.bzl", "envoy_cc_test_library", "envoy_package")
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "common",
    srcs = ["common.cc"],
    hdrs = ["common.h"],
    deps = [
        "//source/extensions/filters/http/cache:cache_headers_utils_lib",
        "//source/extensions/filters/http/cache:http_cache_lib",
        "//source/extensions/filters/http/cache/simple_http_cache:config",
    ],
)

envoy_extension_cc_test(
    name = "cache_headers_utils_test",
    srcs = ["cache_headers_utils_test.cc"],
    extension_names = ["envoy.filters.http.cache"],
    deps = [
        ":common",
        "//envoy/http:header_map_interface",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/cache:cache_headers_utils_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "cache_filter_logging_info_test",
    srcs = ["cache_filter_logging_info_test.cc"],
    extension_names = ["envoy.filters.http.cache"],
    deps = [
        "//source/extensions/filters/http/cache:cache_filter_logging_info_lib",
    ],
)

envoy_extension_cc_test(
    name = "cache_entry_utils_test",
    srcs = ["cache_entry_utils_test.cc"],
    extension_names = ["envoy.filters.http.cache"],
    deps = [
        "//source/extensions/filters/http/cache:cache_entry_utils_lib",
    ],
)

envoy_extension_cc_test(
    name = "http_cache_test",
    srcs = ["http_cache_test.cc"],
    extension_names = ["envoy.filters.http.cache"],
    deps = [
        ":common",
        "//source/extensions/filters/http/cache:http_cache_lib",
        "//source/extensions/filters/http/cache/simple_http_cache:config",
        "//test/mocks/http:http_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "range_utils_test",
    srcs = ["range_utils_test.cc"],
    extension_names = ["envoy.filters.http.cache"],
    deps = [
        ":common",
        "//source/extensions/filters/http/cache:range_utils_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "cache_filter_test",
    srcs = ["cache_filter_test.cc"],
    extension_names = ["envoy.filters.http.cache"],
    deps = [
        ":common",
        "//source/extensions/filters/http/cache:cache_filter_lib",
        "//source/extensions/filters/http/cache:cache_filter_logging_info_lib",
        "//source/extensions/filters/http/cache/simple_http_cache:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

envoy_extension_cc_test(
    name = "cacheability_utils_test",
    srcs = ["cacheability_utils_test.cc"],
    extension_names = ["envoy.filters.http.cache"],
    deps = [
        "//source/extensions/filters/http/cache:cacheability_utils_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.cache"],
    deps = [
        "//source/extensions/filters/http/cache:config",
        "//source/extensions/filters/http/cache/simple_http_cache:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/cache/simple_http_cache/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "cache_filter_integration_test",
    srcs = [
        "cache_filter_integration_test.cc",
    ],
    extension_names = ["envoy.filters.http.cache"],
    shard_count = 2,
    deps = [
        "//source/extensions/filters/http/cache:config",
        "//source/extensions/filters/http/cache:http_cache_lib",
        "//source/extensions/filters/http/cache/simple_http_cache:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_extension_cc_test(
    name = "cache_custom_headers_test",
    srcs = [
        "cache_custom_headers_test.cc",
    ],
    extension_names = ["envoy.filters.http.cache"],
    deps = [
        "//source/extensions/filters/http/cache:cache_custom_headers",
    ],
)

envoy_extension_cc_test_library(
    name = "http_cache_implementation_test_common_lib",
    srcs = ["http_cache_implementation_test_common.cc"],
    hdrs = ["http_cache_implementation_test_common.h"],
    extension_names = ["envoy.filters.http.cache"],
    deps = [
        "//source/extensions/filters/http/cache:cache_headers_utils_lib",
        "//source/extensions/filters/http/cache:http_cache_lib",
        "//test/extensions/filters/http/cache:common",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/synchronization",
        "@envoy_api//envoy/extensions/cache/simple_http_cache/v3:pkg_cc_proto",
    ],
)
