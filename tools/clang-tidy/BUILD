load("@base_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load(":clang_tidy.bzl", "clang_tidy")

licenses(["notice"])  # Apache 2

envoy_package()

clang_tidy(
    name = "clang-tidy",
    target = requirement("clang-tidy"),
)
