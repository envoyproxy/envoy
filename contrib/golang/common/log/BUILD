load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "log_lib",
    srcs = ["cgo.cc"],
    hdrs = [
        "cgo.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//contrib/golang/common/dso:dso_lib",
        "//source/common/common:utility_lib",
    ],
)
