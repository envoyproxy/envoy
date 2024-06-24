load("@base_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_pytool_binary")
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_namespace()

envoy_pytool_binary(
    name = "notify",
    srcs = ["notify.py"],
    args = ["$(location //:reviewers.yaml)"],
    data = ["//:reviewers.yaml"],
    deps = [
        requirement("aio.api.github"),
        requirement("aio.run.runner"),
        requirement("icalendar"),
        requirement("pyyaml"),
        requirement("slack_sdk"),
    ],
)
