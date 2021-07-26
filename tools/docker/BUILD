load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@docker_pip3//:requirements.bzl", "requirement")
load("//tools/base:envoy_python.bzl", "envoy_py_library")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_library(
    name = "tools.docker.utils",
    deps = [
        requirement("aiodocker"),
    ],
)
