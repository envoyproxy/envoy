load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_binary(
    name = "wee8_compile_tool",
    rbe_pool = {"coverage": {"link": "linux_x64_large"}},
    stamp = 0,
    deps = [":wee8_compile_lib"],
)

envoy_cc_library(
    name = "wee8_compile_lib",
    srcs = ["wee8_compile.cc"],
    copts = [
        "-Wno-comments",
        "-Wno-non-virtual-dtor",
        "-Wno-unused-parameter",
    ],
    deps = [
        "@v8//:wee8",
    ],
)
