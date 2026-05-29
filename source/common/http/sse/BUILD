load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "sse_parser_lib",
    srcs = ["sse_parser.cc"],
    hdrs = ["sse_parser.h"],
    deps = [
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
    ],
)
