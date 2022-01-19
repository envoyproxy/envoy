load("@rules_python//python:defs.bzl", "py_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@base_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "finagle_lib",
    srcs = [
        "TFinagleServerProcessor.py",
        "TFinagleServerProtocol.py",
        "__init__.py",
    ],
    deps = [
        requirement("thrift"),
        requirement("twitter.common.finagle-thrift"),
        requirement("twitter.common.rpc"),
    ],
)
