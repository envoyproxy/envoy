load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "utility_lib",
    srcs = [
        "utility.cc",
    ],
    hdrs = ["utility.h"],
    repository = "@envoy",
    deps = [
        "//library/common/bridge:utility_lib",
        "@envoy//source/common/stats:utility_lib",
    ],
)
