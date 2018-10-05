licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "stream_info_interface",
    hdrs = ["stream_info.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":filter_state_interface",
        "//include/envoy/common:time_interface",
        "//include/envoy/http:protocol_interface",
        "//include/envoy/upstream:upstream_interface",
    ],
)

envoy_cc_library(
    name = "filter_state_interface",
    hdrs = ["filter_state.h"],
    external_deps = ["abseil_optional"],
)
