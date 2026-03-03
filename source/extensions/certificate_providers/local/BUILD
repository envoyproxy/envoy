load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "local_certificate_minter_lib",
    srcs = ["local_certificate_minter.cc"],
    hdrs = ["local_certificate_minter.h"],
    deps = [
        "//bazel:ssl",
        "//envoy/common:pure_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
    ],
)
