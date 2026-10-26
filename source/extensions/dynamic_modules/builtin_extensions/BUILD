load("@envoy_rust_crate_index//:defs.bzl", "all_crate_deps")
load("@rules_rust//rust:defs.bzl", "rust_clippy", "rustfmt_test")
load("//bazel:envoy_build_system.bzl", "envoy_extension_package")
load("//source/extensions/dynamic_modules:dynamic_modules.bzl", "envoy_rust_static_module")

licenses(["notice"])  # Apache 2

envoy_extension_package()

exports_files(["Cargo.toml"])

envoy_rust_static_module(
    name = "hickory_dns_static",
    srcs = glob(["*.rs"]),
    crate_root = "lib.rs",
    edition = "2021",
    module_name = "hickory_dns_static",
    visibility = [
        "//source/extensions/network/dns_resolver/hickory:__pkg__",
    ],
    deps = all_crate_deps(
        normal = True,
    ),
)

rustfmt_test(
    name = "fmt_builtin_extensions",
    testonly = True,
    tags = ["nocoverage"],
    targets = [":hickory_dns_static_lib"],
)

rust_clippy(
    name = "clippy_builtin_extensions",
    testonly = True,
    tags = ["nocoverage"],
    deps = [":hickory_dns_static_lib"],
)
