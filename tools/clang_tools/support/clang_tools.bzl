_clang_tools_copts = [
    "-fno-exceptions",
    "-fno-rtti",
]

def clang_tools_cc_binary(name, copts = [], tags = [], deps = [], **kwargs):
    native.cc_binary(
        name = name,
        copts = copts + _clang_tools_copts,
        tags = tags + ["manual"],
        deps = deps + ["@envoy//bazel/foreign_cc:zlib"],
        **kwargs
    )

def clang_tools_cc_library(name, copts = [], **kwargs):
    native.cc_library(
        name = name,
        copts = copts + _clang_tools_copts,
        **kwargs
    )

def clang_tools_cc_test(name, copts = [], deps = [], **kwargs):
    native.cc_test(
        name = name,
        copts = copts + _clang_tools_copts,
        deps = deps + ["@com_google_googletest//:gtest_main"],
        **kwargs
    )
