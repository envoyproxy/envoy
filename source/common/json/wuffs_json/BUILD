load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "wuffs_json_cursor_lib",
    srcs = [
        "wuffs_impl.c",
        "wuffs_json_cursor.cc",
    ],
    hdrs = ["wuffs_json_cursor.h"],
    copts = ["-Wno-unused-function"],
    deps = [
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@wuffs",
    ],
)
