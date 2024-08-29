load("@rules_python//python:defs.bzl", "py_library")
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_py_namespace()

exports_files([
    "gen_git_sha.sh",
    "check_repositories.sh",
])

py_binary(
    name = "stack_decode",
    srcs = ["stack_decode.py"],
)

py_library(
    name = "run_command",
    srcs = [
        "run_command.py",
    ],
    visibility = ["//visibility:public"],
)
