load("@rules_rust//rust:defs.bzl", "rust_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

rust_library(
    name = "envoy_proxy_dynamic_modules_rust_sdk",
    srcs = glob(["src/**/*.rs"]),
    edition = "2021",
)
