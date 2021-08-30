load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_binary", "envoy_py_library")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "distrotest.sh",
])

envoy_py_library(
    name = "tools.distribution.distrotest",
    data = [
        "distrotest.yaml",
    ],
    deps = [
        "//tools/base:checker",
        "//tools/base:utils",
        "//tools/docker:utils",
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
        ":distrotest",
        "//tools/base:checker",
    ],
)
