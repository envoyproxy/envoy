load("@github_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_library")

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "abstract",
    srcs = ["abstract.py"],
    deps = [
        "//tools/base:abstract",
        "//tools/base:functional",
        "//tools/base:utils",
        requirement("aiohttp"),
        requirement("gidgethub"),
        requirement("packaging"),
    ],
)

envoy_py_library(
    "tools.github.release.assets",
    deps = [
        "//tools/base:aio",
        "//tools/base:functional",
        "//tools/base:stream",
        "//tools/base:utils",
        ":abstract",
        ":exceptions",
        requirement("gidgethub"),
        requirement("packaging"),
    ],
)

py_library(
    name = "exceptions",
    srcs = ["exceptions.py"],
)
