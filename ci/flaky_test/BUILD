load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@base_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "process_xml",
    srcs = ["process_xml.py"],
    deps = [
        "@envoy_repo",
        requirement("slackclient"),
    ],
)
