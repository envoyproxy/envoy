licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "request_info_interface",
    hdrs = ["request_info.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":dynamic_metadata_interface",
        "//include/envoy/common:time_interface",
        "//include/envoy/http:protocol_interface",
        "//include/envoy/upstream:upstream_interface",
    ],
)

envoy_cc_library(
    name = "dynamic_metadata_interface",
    hdrs = ["dynamic_metadata.h"],
    external_deps = ["abseil_optional"],
)
