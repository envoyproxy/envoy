load("@rules_cc//cc:defs.bzl", "cc_binary")

licenses(["notice"])  # Apache 2

cc_binary(
    name = "detect",
    srcs = ["detect.cc"],
    local_defines = select({
        "//bazel:gcc_build": ["ENVOY_TOOLCHAIN_GCC_BUILD=1"],
        "//conditions:default": ["ENVOY_TOOLCHAIN_GCC_BUILD=0"],
    }),
    visibility = ["//visibility:public"],
)
