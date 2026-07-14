load("@rules_python//python:defs.bzl", "py_binary", "py_library")

licenses(["notice"])  # Apache 2

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
