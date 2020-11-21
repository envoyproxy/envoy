load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "buffer_interface",
    hdrs = ["buffer.h"],
    external_deps = [
        "abseil_inlined_vector",
    ],
    deps = [
        "//include/envoy/api:os_sys_calls_interface",
        "//source/common/common:byte_order_lib",
        "//source/common/common:utility_lib",
    ],
)
