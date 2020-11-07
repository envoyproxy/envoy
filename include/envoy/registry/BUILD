load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "registry",
    hdrs = ["registry.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/config:api_type_oracle_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
