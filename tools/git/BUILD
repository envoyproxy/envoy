load("@git_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_library")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_library(
    "tools.git.utils",
    deps = [
        requirement("gitpython"),
    ],
)
