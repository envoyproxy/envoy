licenses(["notice"])  # Apache 2

cc_library(
    name = "spdlog",
    hdrs = glob([
        "include/**/*.cc",
        "include/**/*.h",
    ]),
    defines = ["SPDLOG_FMT_EXTERNAL"],
    includes = ["include"],
    linkstatic = 1,
    visibility = ["//visibility:public"],
    deps = ["@com_github_fmtlib_fmt//:fmtlib"],
)
