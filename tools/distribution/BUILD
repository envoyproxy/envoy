load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")
load("@distribution_pip3//:requirements.bzl", "requirement")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")
load("@base_pip3//:requirements.bzl", base_requirement = "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "distrotest.sh",
])

envoy_py_binary(
    name = "tools.distribution.sign",
    deps = [
        requirement("envoy.gpg.identity"),
        base_requirement("envoy.base.runner"),
        base_requirement("envoy.base.utils"),
    ],
)

envoy_py_binary(
    name = "tools.distribution.verify",
    deps = [
        "//tools/base:checker",
        requirement("envoy.distribution.distrotest"),
    ],
)
