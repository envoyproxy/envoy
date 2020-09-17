load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "parser_test",
    srcs = ["parser_test.cc"],
    deps = [
        "//source/extensions/filters/http/cdn_loop:parser_lib",
        "//test/test_common:status_utility_lib",
    ],
)

envoy_cc_test(
    name = "utils_test",
    srcs = ["utils_test.cc"],
    deps = [
        "//source/extensions/filters/http/cdn_loop:utils_lib",
        "//test/test_common:status_utility_lib",
    ],
)
