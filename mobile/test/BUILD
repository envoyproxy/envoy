load("@aspect_bazel_lib//lib:yq.bzl", "yq")

licenses(["notice"])  # Apache 2

yq(
    name = "coverage_config",
    srcs = [":coverage.yaml"],
    outs = ["coverage_config.json"],
    args = ["-o=json"],
    visibility = ["//visibility:public"],
)
