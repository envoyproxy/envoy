package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "version_generated",
    srcs = ["//:version_generated.cc"],
    deps = ["//source/common/common:version_includes"],
)
