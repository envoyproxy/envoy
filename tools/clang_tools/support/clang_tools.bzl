load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

_clang_tools_copts = [
    "-fno-exceptions",
    "-fno-rtti",
]

def clang_tools_cc_binary(name, copts = [], tags = [], deps = [], **kwargs):
    cc_binary(
        name = name,
        copts = copts + _clang_tools_copts,
        tags = tags + ["manual"],
        deps = deps + ["@envoy//bazel/foreign_cc:zlib"],
        **kwargs
    )

def clang_tools_cc_library(name, copts = [], **kwargs):
    cc_library(
        name = name,
        copts = copts + _clang_tools_copts,
        **kwargs
    )

def clang_tools_cc_test(name, copts = [], deps = [], **kwargs):
    cc_test(
        name = name,
        copts = copts + _clang_tools_copts,
        deps = deps + ["@com_google_googletest//:gtest_main"],
        **kwargs
    )
