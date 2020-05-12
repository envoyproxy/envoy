licenses(["notice"])  # Apache 2

load("@config_validation//:requirements.bzl", "requirement")

py_binary(
    name = "validate_yaml_fragment",
    srcs = ["validate_yaml_fragment.py"],
    visibility = ["//visibility:public"],
    deps = [requirement("PyYAML")],
)
