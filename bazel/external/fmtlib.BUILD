load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

cc_library(
    name = "fmtlib",
    hdrs = glob([
        "include/fmt/*.h",
    ]),
    defines = ["FMT_HEADER_ONLY"],
    includes = ["include"],
    visibility = ["//visibility:public"],
)
