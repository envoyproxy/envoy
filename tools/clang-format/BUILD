load("@base_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load(":clang_format.bzl", "clang_format")

licenses(["notice"])  # Apache 2

envoy_package()

clang_format(
    name = "clang-format",
    target = requirement("clang-format"),
)
