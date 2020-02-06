licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

envoy_package()

# TODO(toddmgreer) Change to envoy_extension_cc_test when adding the full filter.
envoy_cc_test(
    name = "http_cache_utils_test",
    srcs = ["http_cache_utils_test.cc"],
    deps = [
        "//include/envoy/http:header_map_interface",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/cache:http_cache_utils_lib",
        "//test/test_common:utility_lib",
    ],
)
