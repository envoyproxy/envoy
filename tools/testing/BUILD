load("@rules_python//python:defs.bzl", "py_library")
load("@testing_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "plugin",
    srcs = ["plugin.py"],
    visibility = ["//visibility:public"],
)

envoy_py_binary(
    "tools.testing.python_pytest",
    data = [
        ":plugin",
        "//:.coveragerc",
        "//:pytest.ini",
        "//tools/base:runner",
        "//tools/base:utils",
    ],
    deps = [
        requirement("pytest"),
        requirement("pytest-cov"),
    ],
)

envoy_py_binary(
    "tools.testing.python_coverage",
    data = [
        ":plugin",
        "//:.coveragerc",
        "//tools/base:runner",
        "//tools/base:utils",
    ],
    deps = [
        requirement("coverage"),
    ],
)
