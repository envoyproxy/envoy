load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "generate_extension_rst",
    srcs = ["generate_extension_rst.py"],
    args = ["$(location //tools/extensions:extension_db)"],
    data = ["//tools/extensions:extension_db"],
    visibility = ["//visibility:public"],
)
