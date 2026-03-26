load("@rules_rust//rust:defs.bzl", "rust_clippy", "rust_static_library", "rustfmt_test")
load("//bazel:envoy_build_system.bzl", "envoy_extension_package")
load("//source/extensions/dynamic_modules:dynamic_modules.bzl", "envoy_dynamic_module_prefix_symbols")

licenses(["notice"])  # Apache 2

envoy_extension_package()

rust_static_library(
    name = "hickory_dns_static_lib",
    srcs = ["hickory_dns.rs"],
    edition = "2021",
    deps = [
        "//source/extensions/dynamic_modules/sdk/rust:envoy_proxy_dynamic_modules_rust_sdk",
        "@dynamic_modules_rust_sdk_crate_index//:hickory-resolver",
        "@dynamic_modules_rust_sdk_crate_index//:serde",
        "@dynamic_modules_rust_sdk_crate_index//:serde_json",
        "@dynamic_modules_rust_sdk_crate_index//:tokio",
        "@dynamic_modules_rust_sdk_crate_index//:url",
    ],
)

envoy_dynamic_module_prefix_symbols(
    name = "hickory_dns_static",
    archive = ":hickory_dns_static_lib",
    module_name = "hickory_dns_static",
    visibility = [
        "//source/extensions/network/dns_resolver/hickory:__pkg__",
    ],
)

rustfmt_test(
    name = "fmt_hickory_dns",
    testonly = True,
    tags = ["nocoverage"],
    targets = [":hickory_dns_static_lib"],
)

rust_clippy(
    name = "clippy_hickory_dns",
    testonly = True,
    tags = ["nocoverage"],
    deps = [":hickory_dns_static_lib"],
)
