load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

licenses(["notice"])  # Apache 2

# Mock implementations for plugin handle interfaces.
# Used for testing.
cc_library(
    name = "sdk_mocks",
    testonly = 1,
    hdrs = ["sdk_mocks.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//source/extensions/dynamic_modules/sdk/cpp:sdk",
        "@googletest//:gtest",
    ],
)

cc_test(
    name = "sdk_mocks_test",
    srcs = ["sdk_mocks_test.cc"],
    tags = [
        "no_san",
        "nocoverage",
    ],
    deps = [
        ":sdk_mocks",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sdk_fake_test",
    srcs = ["sdk_fake_test.cc"],
    tags = [
        "no_san",
        "nocoverage",
    ],
    deps = [
        "//source/extensions/dynamic_modules/sdk/cpp:sdk_fake",
        "@googletest//:gtest_main",
    ],
)
