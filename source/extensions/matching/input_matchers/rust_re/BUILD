load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)
load(
    "@rules_rust//rust:defs.bzl",
    "rust_library",
)
load("@cxx.rs//tools/bazel:rust_cxx_bridge.bzl", "rust_cxx_bridge")

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "rust_re_lib",
    srcs = ["alloc.cc"],
    hdrs = ["matcher.h"],
    deps = [
        ":bridge",
        ":bridge/include",
        ":matcher_rs",
        "//envoy/matcher:matcher_interface",
        "//envoy/upstream:retry_interface",
        "//source/common/common:hash_lib",
        "@cxx.rs//:core",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":rust_re_lib",
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "@envoy_api//envoy/extensions/matching/input_matchers/rust_re/v3:pkg_cc_proto",
    ],
)

rust_library(
    name = "matcher_rs",
    srcs = ["matcher.rs"],
    edition = "2021",
    deps = [
        "bridge",
        "@crate_index//:regex",
        "@cxx.rs//:cxx",
    ],
)

rust_cxx_bridge(
    name = "bridge",
    src = "matcher.rs",
)
