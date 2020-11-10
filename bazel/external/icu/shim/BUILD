load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

licenses(["notice"])  # Apache 2

cc_library(
    name = "common",
    hdrs = [
        "common/unicode/uidna.h",
        "common/unicode/utypes.h",
    ],
    includes = ["common"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "common_test",
    srcs = ["common_test.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":common",
    ],
)

test_suite(
    name = "ci_tests",
    tests = [
        "common_test",
    ],
)
