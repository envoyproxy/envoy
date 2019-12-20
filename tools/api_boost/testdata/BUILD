licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "elaborated_type",
    srcs = ["elaborated_type.cc"],
    deps = ["@envoy_api//envoy/config/overload/v2alpha:pkg_cc_proto"],
)
