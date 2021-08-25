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
        requirement("envoy.gpg.sign"),
    ],
)

envoy_py_binary(
    name = "tools.distribution.verify",
    deps = [
        requirement("envoy.distribution.verify"),
    ],
)
