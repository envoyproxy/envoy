load("@rules_cc//cc:defs.bzl", "cc_library")
load("@envoy_large_machine_exec_property//:constants.bzl", "LARGE_MACHINE")
load(":genrule_cmd.bzl", "genrule_cmd")

licenses(["notice"])  # Apache 2

cc_library(
    name = "wee8",
    srcs = [
        "libwee8.a",
    ],
    hdrs =
        glob([
            "include/**/*.h",
            "src/**/*.h",
            "third_party/wasm-api/wasm.hh",
        ]),
    copts = [
        "-Wno-range-loop-analysis",
    ],
    defines = [
        "V8_ENABLE_WEBASSEMBLY",
    ],
    includes = [
        ".",
        "include",
        "third_party",
    ],
    tags = ["skip_on_windows"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "build",
    srcs = glob(
        ["**"],
        exclude = ["out/**"],
    ),
    outs = [
        "libwee8.a",
    ],
    cmd = genrule_cmd("@envoy//bazel/external:wee8.genrule_cmd"),
    exec_properties = LARGE_MACHINE,
    tags = ["skip_on_windows"],
)
