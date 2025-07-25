load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python/entry_points:py_console_script_binary.bzl", "py_console_script_binary")
load("//tools/base:envoy_python.bzl", "envoy_pytool_binary")
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_py_namespace()

py_console_script_binary(
    name = "release",
    pkg = "@base_pip3//envoy_distribution_release",
    script = "envoy.distribution.release",
    visibility = ["//visibility:public"],
)

py_console_script_binary(
    name = "sign",
    pkg = "@base_pip3//envoy_gpg_sign",
    script = "envoy.gpg.sign",
    visibility = ["//visibility:public"],
)

py_console_script_binary(
    name = "verify",
    pkg = "@base_pip3//envoy_distribution_verify",
    script = "envoy.distribution.verify",
    visibility = ["//visibility:public"],
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
