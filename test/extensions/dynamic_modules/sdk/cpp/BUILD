load("@rules_cc//cc:defs.bzl", "cc_library")

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
