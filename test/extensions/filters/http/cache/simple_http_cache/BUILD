licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_package")
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "simple_http_cache_test",
    srcs = ["simple_http_cache_test.cc"],
    extension_name = "envoy.filters.http.cache.simple_http_cache",
    deps = [
        "//source/extensions/filters/http/cache/simple_http_cache:simple_http_cache_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)
