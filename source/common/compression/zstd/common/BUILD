load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "zstd_base_lib",
    srcs = ["base.cc"],
    hdrs = ["base.h"],
    external_deps = ["zstd"],
    deps = [
        "//source/common/buffer:buffer_lib",
    ],
)
