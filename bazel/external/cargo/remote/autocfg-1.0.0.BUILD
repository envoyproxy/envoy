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
    "notice",  # Apache-2.0 from expression "Apache-2.0 OR MIT"
])

rust_library(
    name = "autocfg",
    srcs = glob(["**/*.rs"]),
    crate_features = [
    ],
    crate_root = "src/lib.rs",
    crate_type = "lib",
    edition = "2015",
    rustc_flags = [
        "--cap-lints=allow",
    ],
    tags = ["cargo-raze"],
    version = "1.0.0",
    deps = [
    ],
)

# Unsupported target "integers" with type "example" omitted
# Unsupported target "paths" with type "example" omitted
# Unsupported target "rustflags" with type "test" omitted
# Unsupported target "traits" with type "example" omitted
# Unsupported target "versions" with type "example" omitted
