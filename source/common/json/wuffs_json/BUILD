load("@rules_cc//cc:cc_library.bzl", "cc_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# wuffs_impl.c is a pure C translation unit (the Wuffs amalgamation).
# It must NOT use envoy_cc_library because envoy_copts() adds C++-only flags
# (-Woverloaded-virtual, -Wold-style-cast) that GCC rejects for C source files.
cc_library(
    name = "wuffs_impl",
    srcs = ["wuffs_impl.c"],
    copts = ["-Wno-unused-function"],
    linkstatic = True,
    deps = ["@wuffs"],
)

envoy_cc_library(
    name = "wuffs_json_cursor_lib",
    srcs = ["wuffs_json_cursor.cc"],
    hdrs = ["wuffs_json_cursor.h"],
    deps = [
        ":wuffs_impl",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@wuffs",
    ],
)
