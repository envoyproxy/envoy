load("@rules_python//python:defs.bzl", "py_test")

licenses(["notice"])  # Apache 2

py_test(
    name = "async_client_fetch_test",
    srcs = ["async_client_fetch_test.py"],
    main = "async_client_fetch_test.py",
    deps = [
        "//library/python:envoy_mobile_lib",
        "//test/python:echo_test_server",
    ],
)
