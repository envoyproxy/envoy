load("//bazel:envoy_build_system.bzl", "envoy_package")
load(":zstd.bzl", "zstd")

licenses(["notice"])  # Apache 2

envoy_package()

zstd(
    name = "zstd",
    target = "//bazel/foreign_cc:zstd",
)
