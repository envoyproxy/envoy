load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:py_binary.bzl", "py_binary")

licenses(["notice"])  # Apache 2

py_binary(
    name = "deprecate_guards",
    srcs = ["deprecate_guards.py"],
    deps = [
        "@envoy_repo",
        requirement("aio.api.github"),
        requirement("aio.run.runner"),
        requirement("gitpython"),
    ],
)
