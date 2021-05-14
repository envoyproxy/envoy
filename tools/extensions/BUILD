load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "generate_extension_db",
    srcs = ["generate_extension_db.py"],
)

py_binary(
    name = "generate_extension_rst",
    srcs = ["generate_extension_rst.py"],
    data = [":generate_extension_db"],
)
