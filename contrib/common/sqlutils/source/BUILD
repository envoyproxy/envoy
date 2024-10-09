load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "sqlutils_lib",
    srcs = ["sqlutils.cc"],
    hdrs = ["sqlutils.h"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "@com_github_envoyproxy_sqlparser//:sqlparser",
    ],
)
