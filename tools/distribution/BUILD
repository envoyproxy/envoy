load("@base_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_entry_point", "envoy_pytool_binary")
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_namespace()

envoy_entry_point(
    name = "release",
    pkg = "envoy.distribution.release",
)

envoy_entry_point(
    name = "sign",
    pkg = "envoy.gpg.sign",
)

envoy_entry_point(
    name = "verify",
    pkg = "envoy.distribution.verify",
)

envoy_pytool_binary(
    name = "update_dockerhub_repository",
    srcs = ["update_dockerhub_repository.py"],
    data = ["//distribution/dockerhub:readme.md"],
    deps = [
        requirement("aio.core"),
        requirement("aio.run.runner"),
        requirement("aiohttp"),
    ],
)
