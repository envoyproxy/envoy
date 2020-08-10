load("@rules_python//python:defs.bzl", "py_binary")
load("@config_validation_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

py_binary(
    name = "validate_fragment",
    srcs = ["validate_fragment.py"],
    data = ["//tools/type_whisperer:all_protos_with_ext_pb_text.pb_text"],
    visibility = ["//visibility:public"],
    deps = [
        requirement("PyYAML"),
        "@bazel_tools//tools/python/runfiles",
        "@com_google_protobuf//:protobuf_python",
    ],
)
