load("@rules_python//python:defs.bzl", "py_binary")
load("@base_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "bazel_query",
    srcs = ["bazel_query.py"],
    main = "bazel_query.py",
    deps = [
        "@envoy_repo",
        requirement("envoy.base.utils"),
    ],
)
