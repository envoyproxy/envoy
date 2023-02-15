load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_gencontent")
load("@base_pip3//:requirements.bzl", "requirement")

load("@dev_pip3//:requirements.bzl", dev_entry_point = "entry_point", dev_requirement = "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_gencontent(
    name = "readme",
    template = ":readme.md.tpl",
    output = "readme.md",
    srcs = ["@envoy_repo//:project"],
    yaml_srcs = [":readme.yaml", "envoy-distroless.yaml"],
)
