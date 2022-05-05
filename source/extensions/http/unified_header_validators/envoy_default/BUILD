load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "unified_header_validator",
    srcs = ["unified_header_validator.cc"],
    hdrs = ["unified_header_validator.h"],
    deps = [
        "//envoy/http:unified_header_validator_interface",
        "@envoy_api//envoy/extensions/http/unified_header_validators/envoy_default/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":unified_header_validator",
        "//envoy/http:unified_header_validator_interface",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/http/unified_header_validators/envoy_default/v3:pkg_cc_proto",
    ],
)
