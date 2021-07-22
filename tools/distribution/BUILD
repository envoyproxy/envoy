load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")
load("@distribution_pip3//:requirements.bzl", "requirement")
load("//tools/base:envoy_python.bzl", "envoy_py_binary", "envoy_py_library")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "distrotest.sh",
])

envoy_py_library(
    name = "tools.distribution.distrotest",
    deps = [
        "//tools/base:checker",
        "//tools/docker:utils",
    ],
)

envoy_py_binary(
    name = "tools.distribution.push",
    deps = [
        "//tools/base:runner",
        "//tools/git:utils",
    ],
)

envoy_py_binary(
    name = "tools.distribution.sign",
    deps = [
        "//tools/base:runner",
        "//tools/base:utils",
        "//tools/gpg:identity",
    ],
)

envoy_py_binary(
    name = "tools.distribution.verify",
    deps = [
        "//tools/base:checker",
        "//tools/docker:utils",
        ":distrotest",
        requirement("pyyaml"),
    ],
)
