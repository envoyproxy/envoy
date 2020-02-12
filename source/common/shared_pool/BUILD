licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "shared_pool_lib",
    srcs = [
        "shared_pool.cc",
    ],
    hdrs = [
        "shared_pool.h",
    ],
    deps = [
        "//include/envoy/singleton:instance_interface",
        "//include/envoy/singleton:manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:non_copyable",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
