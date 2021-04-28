load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

cc_library(
    name = "llhttp",
    srcs = [
        "src/api.c",
        "src/http.c",
        "src/llhttp.c",
    ],
    hdrs = ["include/llhttp.h"],
    defines = ["LLHTTP_STRICT_MODE"],
    includes = ["include"],
    visibility = ["//visibility:public"],
)
