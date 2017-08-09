licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "manager_impl_lib",
    srcs = ["manager_impl.cc"],
    hdrs = ["manager_impl.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/singleton:manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:thread_lib",
    ],
)
