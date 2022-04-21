load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "mutation_rules_lib",
    srcs = ["mutation_rules.cc"],
    hdrs = ["mutation_rules.h"],
    deps = [
        "//envoy/http:header_map_interface",
        "//source/common/common:macros",
        "//source/common/common:regex_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/config/common/mutation_rules/v3:pkg_cc_proto",
    ],
)
