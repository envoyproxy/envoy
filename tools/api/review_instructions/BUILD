load("@rules_python//python:defs.bzl", "py_binary", "py_test")

licenses(["notice"])  # Apache 2

py_binary(
    name = "generate",
    srcs = ["generate.py"],
)

py_test(
    name = "generate_test",
    srcs = ["generate_test.py"],
    deps = [":generate"],
)
