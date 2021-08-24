load("@rules_python//python:defs.bzl", "py_library")
load("@testing_pip3//:requirements.bzl", "requirement")
load("@base_pip3//:requirements.bzl", base_requirement = "requirement")
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
    ],
    deps = [
        requirement("pytest"),
        requirement("pytest-asyncio"),
        requirement("pytest-cov"),
        requirement("pytest-patches"),
        base_requirement("envoy.base.runner"),
        base_requirement("envoy.base.utils"),
    ],
)

envoy_py_binary(
    "tools.testing.python_coverage",
    data = [
        ":plugin",
        "//:.coveragerc",
    ],
    deps = [
        requirement("coverage"),
        base_requirement("envoy.base.runner"),
        base_requirement("envoy.base.utils"),
    ],
)

envoy_py_binary(
    name = "tools.testing.all_pytests",
    deps = [
        base_requirement("envoy.base.checker"),
    ],
)
