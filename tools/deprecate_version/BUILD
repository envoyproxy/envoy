load("@rules_python//python:defs.bzl", "py_binary")
load("@github_pip3//:requirements.bzl", github_requirement = "requirement")

licenses(["notice"])  # Apache 2

py_binary(
    name = "deprecate_version",
    srcs = ["deprecate_version.py"],
    deps = [
        github_requirement("gitpython"),
        github_requirement("pygithub"),
    ],
)
