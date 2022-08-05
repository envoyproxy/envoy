load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "igzip_base_lib",
    srcs = ["base.cc"],
    hdrs = ["base.h"],
    deps = [
        "//bazel/foreign_cc:isa_l",
        "//source/common/buffer:buffer_lib",
    ],
)
