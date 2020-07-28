load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_extension_library(
    name = "sqlutils_lib",
    srcs = ["sqlutils.cc"],
    hdrs = ["sqlutils.h"],
    external_deps = ["sqlparser"],
    deps = [
        "//source/common/protobuf:utility_lib",
    ],
)
