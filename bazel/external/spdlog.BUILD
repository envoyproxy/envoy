licenses(["notice"])  # Apache 2

cc_library(
    name = "spdlog",
    hdrs = glob([
        "include/**/*.h",
    ]),
    defines = [
        "SPDLOG_FMT_EXTERNAL",
        "SPDLOG_NO_EXCEPTIONS",
    ],
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = ["@com_github_fmtlib_fmt//:fmtlib"],
)
