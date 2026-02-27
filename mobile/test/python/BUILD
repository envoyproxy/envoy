load("@rules_python//python:defs.bzl", "py_test")

licenses(["notice"])  # Apache 2

py_test(
    name = "fetch_test",
    srcs = ["fetch_test.py"],
    data = [
        "//library/python:envoy_engine.so",
    ],
    main = "fetch_test.py",
)
