load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        "//library/common/data:utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//envoy/http:codes_interface",
        "@envoy//source/common/http:codes_lib",
    ],
)
