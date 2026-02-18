load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "eventstream_parser_lib",
    srcs = ["eventstream_parser.cc"],
    hdrs = ["eventstream_parser.h"],
    deps = [
        "//bazel:zlib",
        "//source/common/common:assert_lib",
        "//source/common/common:safe_memcpy_lib",
        "//source/common/common:statusor_lib",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
    ],
)
