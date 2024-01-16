load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "data_impl_lib",
    hdrs = ["data_impl.h"],
    deps = [
        "//envoy/network:filter_interface",
    ],
)

envoy_cc_library(
    name = "inputs_lib",
    hdrs = ["inputs.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/matcher:matcher_interface",
        "//envoy/network:filter_interface",
        "//envoy/registry",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/extensions/matching/common_inputs/network/v3:pkg_cc_proto",
    ],
)
