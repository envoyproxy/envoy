load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

cc_library(
    name = "spdlog",
    hdrs = glob([
        "include/**/*.h",
    ]),
    defines = ["SPDLOG_FMT_EXTERNAL"],
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = ["@com_github_fmtlib_fmt//:fmtlib"],
)
