load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

cc_library(
    name = "nlohmann_json_lib",
    hdrs = glob([
        "include/nlohmann/*.hpp",
        "include/nlohmann/**/*.hpp",
        "include/nlohmann/*/*/*.hpp",
    ]),
    includes = ["external/nlohmann_json_lib"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "json",
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [":nlohmann_json_lib"],
)
