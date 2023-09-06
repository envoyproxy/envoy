load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary")

licenses(["notice"])  # Apache 2

py_binary(
    name = "deprecate_version",
    srcs = ["deprecate_version.py"],
    deps = [
        "@envoy_repo",
        requirement("gitpython"),
        requirement("pygithub"),
    ],
)
