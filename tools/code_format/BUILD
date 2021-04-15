load("@rules_python//python:defs.bzl", "py_binary")
load("@pylint_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "check_format.py",
    "header_order.py",
    "envoy_build_fixer.py",
])

py_binary(
    name = "python_flake8",
    srcs = ["python_flake8.py"],
    visibility = ["//visibility:public"],
    deps = [
        requirement("flake8"),
        requirement("pep8-naming"),
    ],
)

py_binary(
    name = "rst_check",
    srcs = ["rst_check.py"],
    visibility = ["//visibility:public"],
)
