load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "constants_lib",
    hdrs = ["constants.h"],
)

envoy_cc_library(
    name = "filter_state_lib",
    hdrs = ["filter_state.h"],
    deps = [
        ":constants_lib",
        "//envoy/json:json_object_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf",
        "//source/common/runtime:runtime_features_lib",
    ],
)
