licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_package")
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

envoy_package()

envoy_extension_cc_test(
    name = "hazelcast_cache_config_test",
    srcs = ["hazelcast_cache_config_test.cc"],
    extension_name = "envoy.filters.http.cache.hazelcast_http_cache",
    deps = [
        ":hazelcast_test_lib",
    ],
)

envoy_extension_cc_test(
    name = "hazelcast_common_cache_test",
    srcs = ["hazelcast_common_cache_test.cc"],
    extension_name = "envoy.filters.http.cache.hazelcast_http_cache",
    deps = [
        ":hazelcast_test_lib",
    ],
)

envoy_extension_cc_test(
    name = "hazelcast_divided_cache_test",
    srcs = ["hazelcast_divided_cache_test.cc"],
    extension_name = "envoy.filters.http.cache.hazelcast_http_cache",
    deps = [
        ":hazelcast_test_lib",
    ],
)

envoy_extension_cc_test(
    name = "hazelcast_entry_serialization_test",
    srcs = ["hazelcast_entry_serialization_test.cc"],
    extension_name = "envoy.filters.http.cache.hazelcast_http_cache",
    deps = [
        ":hazelcast_test_lib",
    ],
)

envoy_extension_cc_test(
    name = "hazelcast_unified_cache_test",
    srcs = ["hazelcast_unified_cache_test.cc"],
    extension_name = "envoy.filters.http.cache.hazelcast_http_cache",
    deps = [
        ":hazelcast_test_lib",
    ],
)

envoy_extension_cc_test_library(
    name = "hazelcast_test_lib",
    hdrs = [
        "test_accessors.h",
        "test_caches.h",
        "test_util.h",
    ],
    extension_name = "envoy.filters.http.cache.hazelcast_http_cache",
    deps = [
        "//source/extensions/filters/http/cache/hazelcast_http_cache:hazelcast_http_cache_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)
