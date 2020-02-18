licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_package")
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "http_cache_utils_test",
    srcs = ["http_cache_utils_test.cc"],
    extension_name = "envoy.filters.http.cache",
    deps = [
        "//include/envoy/http:header_map_interface",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/cache:http_cache_utils_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "http_cache_test",
    srcs = ["http_cache_test.cc"],
    extension_name = "envoy.filters.http.cache",
    deps = [
        "//source/extensions/filters/http/cache:http_cache_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)
