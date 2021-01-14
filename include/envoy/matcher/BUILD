load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "matcher_interface",
    hdrs = ["matcher.h"],
    external_deps = [
        "abseil_base",
        "abseil_hash",
    ],
    deps = [
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/protobuf:message_validator_interface",
        "@envoy_api//envoy/config/common/matcher/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
