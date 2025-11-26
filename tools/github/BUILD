load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:py_binary.bzl", "py_binary")

licenses(["notice"])  # Apache 2

py_binary(
    name = "sync_assignable",
    srcs = ["sync_assignable.py"],
    deps = [requirement("pygithub")],
)
