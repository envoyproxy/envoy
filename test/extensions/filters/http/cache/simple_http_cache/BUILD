load("//bazel:envoy_build_system.bzl", "envoy_package")
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "simple_http_cache_test",
    srcs = ["simple_http_cache_test.cc"],
    extension_names = ["envoy.extensions.http.cache.simple"],
    deps = [
        "//source/extensions/filters/http/cache:cache_entry_utils_lib",
        "//source/extensions/filters/http/cache/simple_http_cache:config",
        "//test/extensions/filters/http/cache:common",
        "//test/extensions/filters/http/cache:http_cache_implementation_test_common_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)
