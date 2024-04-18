load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary")

licenses(["notice"])  # Apache 2

py_binary(
    name = "validate_fragment",
    srcs = ["validate_fragment.py"],
    args = ["$(location @envoy_api//:v3_proto_set)"],
    data = ["@envoy_api//:v3_proto_set"],
    visibility = ["//visibility:public"],
    deps = [
        requirement("envoy.base.utils"),
        requirement("PyYAML"),
        "@com_google_protobuf//:protobuf_python",
    ],
)
