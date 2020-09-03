load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "cdn_loop_parser_test",
    srcs = ["cdn_loop_parser_test.cc"],
    deps = [
        "//source/extensions/filters/http/cdn_loop:cdn_loop_parser_lib",
        "//test/test_common:status_utility_lib",
    ],
)

envoy_cc_test(
    name = "cdn_loop_utils_test",
    srcs = ["cdn_loop_utils_test.cc"],
    deps = [
        "//source/extensions/filters/http/cdn_loop:cdn_loop_utils_lib",
        "//test/test_common:status_utility_lib",
    ],
)
