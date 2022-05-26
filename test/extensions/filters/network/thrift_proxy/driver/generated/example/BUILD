load("@rules_python//python:defs.bzl", "py_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@base_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "example_lib",
    srcs = [
        "Example.py",
        "__init__.py",
        "constants.py",
        "ttypes.py",
    ],
    deps = [
        requirement("thrift"),
    ],
)
