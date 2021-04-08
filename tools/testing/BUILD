load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("@testing_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "plugin",
    srcs = ["plugin.py"],
    visibility = ["//visibility:public"],
)

py_binary(
    name = "python_pytest",
    srcs = ["python_pytest.py"],
    data = [
        ":plugin",
        "//:.coveragerc",
        "//:pytest.ini",
        "//tools/base:runner",
        "//tools/base:utils",
    ],
    visibility = ["//visibility:public"],
    deps = [
        requirement("pytest"),
        requirement("pytest-cov"),
    ],
)

py_binary(
    name = "pytest_python_pytest",
    srcs = [
        "pytest_python_pytest.py",
        "tests/test_python_pytest.py",
    ],
    deps = [
        ":python_pytest",
    ],
)

py_binary(
    name = "python_coverage",
    srcs = ["python_coverage.py"],
    data = [
        ":plugin",
        "//:.coveragerc",
        "//:pytest.ini",
        "//tools/base:runner",
        "//tools/base:utils",
    ],
    visibility = ["//visibility:public"],
    deps = [
        requirement("coverage"),
    ],
)

py_binary(
    name = "pytest_python_coverage",
    srcs = [
        "pytest_python_coverage.py",
        "tests/test_python_coverage.py",
    ],
    deps = [
        ":python_coverage",
        "//tools/testing:python_pytest",
    ],
)
