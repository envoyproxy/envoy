licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "zlib_lib",
    srcs = ["zlib_impl.cc"],
    hdrs = ["zlib_impl.h"],
    external_deps = ["zlib"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/buffer:buffer_lib",
        "//include/envoy/zlib:zlib_interface",
    ],
)