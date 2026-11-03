load("@aspect_bazel_lib//lib:yq.bzl", "yq")
load("@envoy_toolshed//coverage:macros.bzl", "coverage_report")

licenses(["notice"])  # Apache 2

yq(
    name = "coverage_config",
    srcs = [":coverage.yaml"],
    outs = ["coverage_config.json"],
    args = ["-o=json"],
    visibility = ["//visibility:public"],
)

label_flag(
    name = "config",
    build_setting_default = ":coverage_config",
    visibility = ["//visibility:public"],
)

coverage_report(
    name = "coverage_report",
    config = ":config",
    source_root = "library",
    visibility = ["//visibility:public"],
)
