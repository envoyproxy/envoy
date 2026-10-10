load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "header_parser_lib",
    srcs = ["header_parser.cc"],
    hdrs = ["header_parser.h"],
    deps = [
        "@abseil-cpp//absl/strings",
    ],
)
