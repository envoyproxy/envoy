load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_gencontent")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_gencontent(
    name = "readme",
    srcs = ["@envoy_repo//:project"],
    output = "readme.md",
    template = ":readme.md.tpl",
    yaml_srcs = [":readme.yaml"],
)
