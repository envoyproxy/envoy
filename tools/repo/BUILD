load("@base_pip3//:requirements.bzl", "requirement")
load("//tools/base:envoy_python.bzl", "envoy_pytool_binary")
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_py_namespace()

envoy_pytool_binary(
    name = "notify",
    srcs = ["notify.py"],
    args = ["$(location //:reviewers.yaml)"],
    data = ["//:reviewers.yaml"],
    visibility = ["//visibility:public"],
    deps = [
        requirement("aio.api.github"),
        requirement("aio.run.runner"),
        requirement("icalendar"),
        requirement("pyyaml"),
        requirement("slack_sdk"),
    ],
)

envoy_pytool_binary(
    name = "security_notify",
    srcs = ["security_notify.py"],
    visibility = ["//visibility:public"],
    deps = [
        requirement("aio.run.runner"),
        requirement("aiohttp"),
        requirement("slack_sdk"),
    ],
)
