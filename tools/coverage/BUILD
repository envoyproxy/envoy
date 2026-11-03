load("@envoy_toolshed//coverage:macros.bzl", "coverage_report")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

label_flag(
    name = "config",
    build_setting_default = "//test:coverage_config",
    visibility = ["//visibility:public"],
)

coverage_report(
    name = "report",
    config = ":config",
    visibility = ["//visibility:public"],
)
