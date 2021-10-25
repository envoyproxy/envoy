load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "check_format.py",
    "header_order.py",
    "envoy_build_fixer.py",
])

py_binary(
    name = "python_check",
    srcs = ["python_check.py"],
    deps = [
        "@envoy_repo",
        requirement("envoy.code_format.python_check"),
    ],
)
