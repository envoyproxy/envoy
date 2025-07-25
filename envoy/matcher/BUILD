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
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/protobuf:message_validator_interface",
        "@com_github_cncf_xds//xds/type/matcher/v3:pkg_cc_proto",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/hash",
        "@envoy_api//envoy/config/common/matcher/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
