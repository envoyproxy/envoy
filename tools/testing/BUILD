load("@rules_python//python:defs.bzl", "py_library")
load("@base_pip3//:requirements.bzl", "requirement")
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
        requirement("aio.run.runner"),
        requirement("envoy.base.utils"),
        requirement("nest-asyncio"),
        requirement("pytest"),
        requirement("pytest-asyncio"),
        requirement("pytest-cov"),
        requirement("pytest-patches"),
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
        requirement("aio.run.runner"),
        requirement("envoy.base.utils"),
    ],
)

envoy_py_binary(
    name = "tools.testing.all_pytests",
    deps = [
        "@envoy_repo",
        requirement("aio.api.bazel"),
        requirement("aio.core"),
        requirement("aio.run.checker"),
    ],
)
