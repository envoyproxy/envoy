load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

# All necessary abstractions for creating plugins.
cc_library(
    name = "sdk",
    srcs = ["sdk.cc"],
    hdrs = ["sdk.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
    ],
    alwayslink = True,
)

# SDK implementation and underlying ABI selection.
# Link this library and the :sdk to final output binaries.
# The example practice is to link against :sdk only in plugins,
# then you can test the plugins with mocks.
# And then you a new bazel target to link the :sdk_abi and
# the plugins lib.
cc_library(
    name = "sdk_abi",
    srcs = ["sdk_internal.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":sdk",
        "//source/extensions/dynamic_modules/abi",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/synchronization",
    ],
    alwayslink = True,
)

# Fake implementations for simple type like header map or buffer.
# Used for testing.
cc_library(
    name = "sdk_fake",
    hdrs = ["sdk_fake.h"],
    visibility = ["//visibility:public"],
    deps = [":sdk"],
)

# Mock implementations for plugin handle interfaces.
# Used for testing.
cc_library(
    name = "sdk_mocks",
    hdrs = ["sdk_mocks.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":sdk",
        "@com_google_googletest//:gtest",
    ],
)
