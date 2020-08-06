load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

exports_files(["LICENSE"])

icuuc_copts = [
    "-DU_STATIC_IMPLEMENTATION",
    "-DU_COMMON_IMPLEMENTATION",
    "-DU_HAVE_STD_ATOMICS",
] + select({
    "@envoy//bazel:apple": [
        "-Wno-shorten-64-to-32",
        "-Wno-unused-variable",
    ],
    "@envoy//bazel:windows_x86_64": [
        "/utf-8",
        "/DLOCALE_ALLOW_NEUTRAL_NAMES=0",
    ],
    # TODO(dio): Add "@envoy//bazel:android" when we have it.
    # "@envoy//bazel:android": [
    #     "-fdata-sections",
    #     "-DU_HAVE_NL_LANGINFO_CODESET=0",
    #     "-Wno-deprecated-declarations",
    # ],
    "//conditions:default": [],
})

cc_library(
    name = "headers",
    hdrs = glob(["source/common/unicode/*.h"]),
    includes = ["source/common"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "common",
    hdrs = glob(["source/common/unicode/*.h"]),
    includes = ["source/common"],
    visibility = ["//visibility:public"],
    deps = [":icuuc"],
)

cc_library(
    name = "icuuc",
    srcs = glob([
        "source/common/*.c",
        "source/common/*.cpp",
        "source/stubdata/*.cpp",
    ]),
    hdrs = glob(["source/common/*.h"]),
    copts = icuuc_copts,
    visibility = ["//visibility:private"],
    deps = [":headers"],
)
