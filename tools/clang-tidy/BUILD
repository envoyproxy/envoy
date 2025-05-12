load("@base_pip3//:requirements.bzl", "requirement")
load(":clang_tidy.bzl", "clang_tidy")

licenses(["notice"])  # Apache 2

clang_tidy(
    name = "clang-tidy",
    target = requirement("clang-tidy"),
)
