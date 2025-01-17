load("@base_pip3//:requirements.bzl", "requirement")
load(":clang_format.bzl", "clang_format")

licenses(["notice"])  # Apache 2

clang_format(
    name = "clang-format",
    target = requirement("clang-format"),
    visibility = ["//visibility:public"],
)
