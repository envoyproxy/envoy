load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "regex_mutation_lib",
    srcs = ["regex_mutation.cc"],
    hdrs = ["regex_mutation.h"],
    deps = [
        "//envoy/http:early_header_mutation_interface",
        "//source/common/common:regex_lib",
        "@envoy_api//envoy/extensions/http/early_header_mutation/regex_mutation/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/http/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":regex_mutation_lib",
        "//envoy/http:early_header_mutation_interface",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/http/early_header_mutation/regex_mutation/v3:pkg_cc_proto",
    ],
)
