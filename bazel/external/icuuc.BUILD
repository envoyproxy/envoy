# https://github.com/tensorflow/tensorflow/commit/1154432f88387c81be4ac2e3cb249b787ffafe21
# https://github.com/tensorflow/tensorflow/blob/068641e7f858d92407343b2c3994d3bee3822093/third_party/icu/BUILD.bazel
licenses(["notice"])  # Apache 2

exports_files([
    "icu4c/LICENSE",
    "icu4j/main/shared/licenses/LICENSE",
])

icuuc_copts = [
    "-DU_COMMON_IMPLEMENTATION",
    "-DU_HAVE_STD_ATOMICS",  # TODO(gunan): Remove when TF is on ICU 64+.
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
    hdrs = glob(["icu4c/source/common/unicode/*.h"]),
    includes = ["icu4c/source/common"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "common",
    hdrs = glob(["icu4c/source/common/unicode/*.h"]),
    includes = ["icu4c/source/common"],
    visibility = ["//visibility:public"],
    deps = [":icuuc"],
)

cc_library(
    name = "icuuc",
    srcs = glob([
        "icu4c/source/common/*.c",
        "icu4c/source/common/*.cpp",
        "icu4c/source/stubdata/*.cpp",
    ]),
    hdrs = glob(["icu4c/source/common/*.h"]),
    copts = icuuc_copts,
    tags = ["requires-rtti"],
    visibility = ["//visibility:private"],
    deps = [":headers"],
)
