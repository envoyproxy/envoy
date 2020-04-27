licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "shared_pool_lib",
    hdrs = [
        "shared_pool.h",
    ],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/singleton:instance_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:non_copyable",
        "//source/common/common:thread_synchronizer_lib",
    ],
)
