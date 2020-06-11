load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "sqlutils_lib",
    srcs = ["sqlutils.cc"],
    hdrs = ["sqlutils.h"],
    external_deps = ["sqlparser"],
    deps = [
        "//source/common/protobuf:utility_lib",
    ],
)
