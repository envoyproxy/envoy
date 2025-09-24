load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        "@envoy//envoy/buffer:buffer_interface",
        "@envoy//envoy/http:codes_interface",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/common:empty_string",
        "@envoy//source/common/http:codes_lib",
        "@envoy_mobile//library/common:engine_types_lib",
        "@envoy_mobile//library/common/types:c_types_lib",
    ],
)
