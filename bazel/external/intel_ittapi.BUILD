load("@rules_cc//cc:cc_library.bzl", "cc_library")

licenses(["notice"])

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "intel_ittapi",
    srcs = glob([
        "src/ittnotify/*.c",
        "src/ittnotify/*.h",
    ]),
    hdrs = glob([
        "include/*.h",
        "src/ittnotify/*.h",
    ]),
    copts = [
        "-DITTNOTIFY_STATIC",
    ],
    includes = [
        "include",
        "src/ittnotify",
    ],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": ["-ldl"],
    }),
    local_defines = [
        "ITTNOTIFY_STATIC",
    ],
)
