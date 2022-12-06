load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_rust_library",
)
load(
    "@rules_rust//rust:defs.bzl",
    "rust_library",
)
load("@cxx.rs//tools/bazel:rust_cxx_bridge.bzl", "rust_cxx_bridge")

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "consistent_hashing_lib",
    hdrs = ["matcher.h"],
    deps = [
        "bridge",
        "bridge/include",
        ":matcher_rs",
        "//envoy/matcher:matcher_interface",
        "//envoy/upstream:retry_interface",
        "@cxx.rs//:core",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":consistent_hashing_lib",
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "@envoy_api//envoy/extensions/matching/input_matchers/consistent_hashing/v3:pkg_cc_proto",
    ],
)

envoy_rust_library(
    name = "matcher_rs",
    srcs = ["matcher.rs"],
    deps = [
        "bridge",
        "@crate_index//:regex",
        "@cxx.rs//:cxx",
    ],
)

rust_cxx_bridge(
    name = "bridge",
    src = "matcher.rs",
    deps = ["hash_lib"],
)

envoy_cc_library(
    name = "hash_lib",
    srcs = ["hash.cc"],
    hdrs = ["hash.h"],
    deps = [
        "//source/common/common:hash_lib",
        "@cxx.rs//:core",
    ],
)
