load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "external_buffer_interface",
    hdrs = ["external_buffer.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/common:pure_lib",
        "//envoy/event:dispatcher_interface",
        "@abseil-cpp//absl/functional:any_invocable",
    ],
)

envoy_cc_library(
    name = "filter_chain_bridge_interface",
    hdrs = ["filter_chain_bridge.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/common:pure_lib",
        "//envoy/event:dispatcher_interface",
        "@abseil-cpp//absl/functional:any_invocable",
    ],
)
