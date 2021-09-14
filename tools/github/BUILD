load("@rules_python//python:defs.bzl", "py_binary")
load("@base_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

py_binary(
    name = "sync_assignable",
    srcs = ["sync_assignable.py"],
    deps = [requirement("pygithub")],
)
