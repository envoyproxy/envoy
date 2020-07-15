load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "external_api_lib",
    srcs = [
        "external.cc",
    ],
    hdrs = [
        "external.h",
    ],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        "@envoy//source/common/common:assert_lib",
    ],
)
