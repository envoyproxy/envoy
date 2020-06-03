load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

def clang_tools_cc_binary(name, copts = [], tags = [], deps = [], **kwargs):
    cc_binary(
        name = name,
        copts = copts + [
            "-fno-exceptions",
            "-fno-rtti",
        ],
        tags = tags + ["manual"],
        deps = deps + ["@envoy//bazel/foreign_cc:zlib"],
        **kwargs
    )

def clang_tools_cc_library(name, **kwargs):
    cc_library(
        name = name,
        **kwargs
    )

def clang_tools_cc_test(name, deps = [], **kwargs):
    cc_test(
        name = name,
        deps = deps + ["@com_google_googletest//:gtest_main"],
        **kwargs
    )
