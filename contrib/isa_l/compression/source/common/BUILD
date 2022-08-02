load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
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
        "//source/common/buffer:buffer_lib",
        "@envoy//bazel/foreign_cc:isa_l",
    ],
)
