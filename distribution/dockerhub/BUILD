load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_gencontent")
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_namespace()

envoy_gencontent(
    name = "readme",
    srcs = ["@envoy_repo//:project"],
    output = "readme.md",
    template = ":readme.md.tpl",
    yaml_srcs = [":readme.yaml"],
)
