load("@pylint_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "check_format.py",
    "header_order.py",
    "envoy_build_fixer.py",
])

envoy_py_binary(
    name = "tools.code_format.python_check",
    deps = [
        "//tools/base:checker",
        "//tools/base:utils",
        requirement("flake8"),
        requirement("pep8-naming"),
        requirement("yapf"),
    ],
)
