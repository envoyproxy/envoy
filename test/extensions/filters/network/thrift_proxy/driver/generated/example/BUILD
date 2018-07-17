licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_package")

envoy_package()

py_library(
    name = "example_lib",
    srcs = [
        "Example.py",
        "__init__.py",
        "constants.py",
        "ttypes.py",
    ],
    deps = [
        "@com_github_apache_thrift//:apache_thrift",
    ],
)
