licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

envoy_package()

envoy_cc_test(
    name = "simple_http_cache_test",
    srcs = ["simple_http_cache_test.cc"],
    deps = [
        "//source/extensions/filters/http/cache/simple_http_cache:simple_http_cache_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)
