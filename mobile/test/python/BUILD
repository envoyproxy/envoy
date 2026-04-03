load("@mobile_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_library", "py_test")

licenses(["notice"])  # Apache 2

py_library(
    name = "echo_test_server",
    srcs = ["echo_test_server.py"],
    visibility = ["//visibility:public"],
)

py_test(
    name = "fetch_test",
    srcs = ["fetch_test.py"],
    main = "fetch_test.py",
    deps = [
        ":echo_test_server",
        "//library/python:envoy_mobile_lib",
    ],
)

py_test(
    name = "lifecycle_test",
    srcs = ["lifecycle_test.py"],
    main = "lifecycle_test.py",
    deps = [
        "//library/python:envoy_mobile_lib",
    ],
)

py_test(
    name = "httpx_transport_test",
    srcs = ["test_httpx_transport.py"],
    main = "test_httpx_transport.py",
    deps = [
        "//library/python:envoy_mobile_lib",
        requirement("httpx"),
    ],
)

py_test(
    name = "transport_factory_test",
    srcs = ["test_transport_factory.py"],
    main = "test_transport_factory.py",
    deps = [
        "//library/python:envoy_mobile_lib",
        requirement("httpx"),
    ],
)
