load("@rules_python//python:defs.bzl", "py_test")

licenses(["notice"])  # Apache 2

py_test(
    name = "fetch_test",
    srcs = ["fetch_test.py"],
    main = "fetch_test.py",
    deps = ["//library/python:envoy_engine"],
)
