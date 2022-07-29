load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "header_validator",
    srcs = [
        "header_validator.cc",
        "header_validator_factory.cc",
        "http1_header_validator.cc",
        "http2_header_validator.cc",
        "null_header_validator.cc",
    ],
    hdrs = [
        "character_tables.h",
        "header_validator.h",
        "header_validator_factory.h",
        "http1_header_validator.h",
        "http2_header_validator.h",
        "null_header_validator.h",
    ],
    deps = [
        "//envoy/http:header_validator_interface",
        "//external:abseil_node_hash_map",
        "//external:abseil_node_hash_set",
        "//source/common/http:headers_lib",
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":header_validator",
        "//envoy/http:header_validator_interface",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/http/header_validators/envoy_default/v3:pkg_cc_proto",
    ],
)
