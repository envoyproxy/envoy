load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "c_types_lib",
    srcs = [
        "c_types.cc",
    ],
    hdrs = [
        "c_types.h",
    ],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        "@envoy//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "matcher_data_lib",
    srcs = [
        "matcher_data.h",
    ],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        "//library/common/types:c_types_lib",
    ],
)

envoy_cc_library(
    name = "managed_types_lib",
    srcs = [
        "managed_envoy_headers.h",
    ],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        "//library/common/types:c_types_lib",
    ],
)
