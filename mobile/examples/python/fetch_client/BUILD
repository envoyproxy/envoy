load("@rules_python//python:defs.bzl", "py_binary")

licenses(["notice"])  # Apache 2

py_binary(
    name = "fetch_client",
    srcs = ["fetch_client.py"],
    deps = ["//library/python:envoy_engine"],
)
