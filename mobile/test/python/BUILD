load("@rules_python//python:defs.bzl", "py_library", "py_test")

licenses(["notice"])  # Apache 2

py_test(
    name = "fetch_test",
    srcs = ["fetch_test.py"],
    data = [
        "//library/python:envoy_engine.so",
    ],
    main = "fetch_test.py",
    deps = [
        ":echo_test_server",
    ],
)

py_test(
    name = "lifecycle_test",
    srcs = ["lifecycle_test.py"],
    data = [
        "//library/python:envoy_engine.so",
    ],
    main = "lifecycle_test.py",
)

py_library(
    name = "echo_test_server",
    srcs = ["echo_test_server.py"],
)
