load("@github_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "abstract",
    srcs = ["abstract.py"],
    deps = [
        "//tools/base:functional",
        "//tools/base:utils",
        requirement("aiohttp"),
        requirement("gidgethub"),
        requirement("packaging"),
    ],
)

py_library(
    name = "exceptions",
    srcs = ["exceptions.py"],
)
