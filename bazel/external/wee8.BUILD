load("@rules_cc//cc:defs.bzl", "cc_library")
load(":genrule_cmd.bzl", "genrule_cmd")

licenses(["notice"])  # Apache 2

cc_library(
    name = "wee8",
    srcs = [
        "libwee8.a",
    ],
    hdrs = [
        "wee8/include/v8-version.h",
        "wee8/third_party/wasm-api/wasm.hh",
    ],
    includes = [
        "wee8/include",
        "wee8/third_party",
    ],
    visibility = ["//visibility:public"],
)

genrule(
    name = "build",
    srcs = glob(["wee8/**"]),
    outs = [
        "libwee8.a",
    ],
    cmd = genrule_cmd("@envoy//bazel/external:wee8.genrule_cmd"),
)
