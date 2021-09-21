load("@rules_python//python:defs.bzl", "py_binary")
load("@base_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_library")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_library(
    "tools.base.aio",
    deps = [
        requirement("aio.functional"),
    ],
)

envoy_py_library(
    "tools.base.checker",
    deps = [
        ":runner",
    ],
)

envoy_py_library(
    "tools.base.runner",
    deps = [
        requirement("colorama"),
        requirement("coloredlogs"),
        requirement("frozendict"),
        requirement("verboselogs"),
    ],
)

envoy_py_library(
    "tools.base.utils",
    deps = [
        requirement("pyyaml"),
        requirement("setuptools"),
    ],
)

py_binary(
    name = "bazel_query",
    srcs = ["bazel_query.py"],
    main = "bazel_query.py",
    deps = [
        "@envoy_repo",
        requirement("envoy.base.utils"),
    ],
)
