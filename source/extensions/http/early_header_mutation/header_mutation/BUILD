load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "header_mutation_lib",
    srcs = ["header_mutation.cc"],
    hdrs = ["header_mutation.h"],
    deps = [
        "//envoy/http:early_header_mutation_interface",
        "//source/common/http:header_mutation_lib",
        "@envoy_api//envoy/config/common/mutation_rules/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/http/early_header_mutation/header_mutation/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":header_mutation_lib",
        "//envoy/http:early_header_mutation_interface",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/http/early_header_mutation/header_mutation/v3:pkg_cc_proto",
    ],
)
