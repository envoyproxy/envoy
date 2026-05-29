load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary")

licenses(["notice"])  # Apache 2

py_binary(
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

py_binary(
    name = "security_notify",
    srcs = ["security_notify.py"],
    visibility = ["//visibility:public"],
    deps = [
        requirement("aio.run.runner"),
        requirement("aiohttp"),
        requirement("slack_sdk"),
    ],
)
