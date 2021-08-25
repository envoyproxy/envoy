load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")
load("@distribution_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "distrotest.sh",
])

envoy_py_binary(
    name = "tools.distribution.sign",
    deps = [
        "//tools/base:runner",
        "//tools/base:utils",
        requirement("envoy.gpg.identity"),
    ],
)

envoy_py_binary(
    name = "tools.distribution.verify",
    deps = [
        "//tools/base:checker",
        requirement("envoy.distribution.distrotest"),
    ],
)
