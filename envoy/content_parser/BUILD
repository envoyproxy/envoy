load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "parser_interface",
    hdrs = ["parser.h"],
    deps = [
        "//envoy/common:pure_lib",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "factory_interface",
    hdrs = ["factory.h"],
    deps = [
        ":parser_interface",
    ],
)

envoy_cc_library(
    name = "config_interface",
    hdrs = ["config.h"],
    deps = [
        ":factory_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/server:factory_context_interface",
    ],
)
