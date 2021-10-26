load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "data_lib",
    hdrs = ["data.h"],
    deps = [
        "//source/common/network:cidr_range_lib",
    ],
)

envoy_cc_library(
    name = "data_impl_lib",
    hdrs = ["data_impl.h"],
    deps = [
        ":data_lib",
    ],
)

envoy_cc_library(
    name = "inputs_lib",
    srcs = ["inputs.cc"],
    hdrs = ["inputs.h"],
    deps = [
        ":data_impl_lib",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)
