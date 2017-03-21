package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_external_dep_path")

cc_library(
    name = "precompiled_includes",
    hdrs = ["precompiled.h"],
    deps = [envoy_external_dep_path("spdlog")],
)
