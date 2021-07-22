load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_binary(
    name = "tools.distribution.sign",
    deps = [
        "//tools/base:runner",
        "//tools/base:utils",
        "//tools/gpg:identity",
    ],
)
