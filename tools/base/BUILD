load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "runner",
    srcs = ["runner.py"],
)

py_binary(
    name = "pytest_runner",
    srcs = [
        "pytest_runner.py",
        "tests/test_runner.py",
    ],
    deps = [
        ":runner",
        "//tools/testing:python_pytest",
    ],
)

py_library(
    name = "utils",
    srcs = ["utils.py"],
)

py_binary(
    name = "pytest_utils",
    srcs = [
        "pytest_utils.py",
        "tests/test_utils.py",
    ],
    deps = [
        ":utils",
        "//tools/testing:python_pytest",
    ],
)
