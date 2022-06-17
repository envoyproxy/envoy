load("@rules_python//python:defs.bzl", "py_binary")
load("@base_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

py_binary(
    name = "validate_fragment",
    srcs = ["validate_fragment.py"],
    args = ["--descriptor_path=$(location @envoy_api//:v3_proto_set)"],
    data = ["@envoy_api//:v3_proto_set"],
    visibility = ["//visibility:public"],
    deps = [
        requirement("envoy.base.utils"),
        requirement("PyYAML"),
        "@bazel_tools//tools/python/runfiles",
        "@com_google_protobuf//:protobuf_python",
    ],
)
