load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "status_helpers",
    hdrs = ["status_helpers.h"],
)

envoy_cc_test(
    name = "cdn_loop_parser_test",
    srcs = ["cdn_loop_parser_test.cc"],
    deps = [
        "status_helpers",
        "//source/extensions/filters/http/cdn:cdn_loop_parser",
    ],
)

envoy_cc_test(
    name = "cdn_loop_utils_test",
    srcs = ["cdn_loop_utils_test.cc"],
    deps = [
        ":status_helpers",
        "//source/extensions/filters/http/cdn:cdn_loop_utils",
    ],
)
