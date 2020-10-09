"""
cargo-raze crate build file.

DO NOT EDIT! Replaced on runs of cargo-raze
"""

load(
    "@io_bazel_rules_rust//rust:rust.bzl",
    "rust_library",
)

package(default_visibility = [
    # Public for visibility by "@raze__crate__version//" targets.
    #
    # Prefer access through "//bazel/external/cargo", which limits external
    # visibility to explicit Cargo.toml dependencies.
    "//visibility:public",
])

licenses([
    "reciprocal",  # MPL-2.0 from expression "MPL-2.0"
])

# Unsupported target "build-script-build" with type "custom-build" omitted

rust_library(
    name = "wee_alloc",
    srcs = glob(["**/*.rs"]),
    crate_features = [
        "default",
        "size_classes",
    ],
    crate_root = "src/lib.rs",
    crate_type = "lib",
    edition = "2015",
    rustc_flags = [
        "--cap-lints=allow",
    ],
    tags = ["cargo-raze"],
    version = "0.4.5",
    deps = [
        "@raze__cfg_if__0_1_10//:cfg_if",
        "@raze__libc__0_2_74//:libc",
        "@raze__memory_units__0_4_0//:memory_units",
    ],
)
