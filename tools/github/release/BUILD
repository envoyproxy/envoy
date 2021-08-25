load("@github_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_library")
load("@base_pip3//:requirements.bzl", base_requirement = "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "abstract",
    srcs = ["abstract.py"],
    deps = [
        requirement("abstracts"),
        requirement("aio.functional"),
        requirement("aiohttp"),
        requirement("gidgethub"),
        requirement("packaging"),
    ],
)

py_library(
    name = "exceptions",
    srcs = ["exceptions.py"],
)

envoy_py_library(
    "tools.github.release.manager",
    deps = [
        ":abstract",
        ":exceptions",
        base_requirement("aio.functional"),
        base_requirement("envoy.base.utils"),
        requirement("abstracts"),
        requirement("aiohttp"),
        requirement("gidgethub"),
        requirement("packaging"),
    ],
)
