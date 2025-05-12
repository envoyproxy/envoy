load(
    "//bazel:envoy_build_system.bzl",
    "envoy_py_test_binary",
)

licenses(["notice"])  # Apache 2

envoy_py_test_binary(
    name = "socket_passing",
    srcs = [
        "socket_passing.py",
    ],
    visibility = ["//visibility:public"],
)
