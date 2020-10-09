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
    "notice",  # MIT from expression "MIT OR Apache-2.0"
])

# Unsupported target "ahash" with type "bench" omitted

rust_library(
    name = "ahash",
    srcs = glob(["**/*.rs"]),
    crate_features = [
    ],
    crate_root = "src/lib.rs",
    crate_type = "lib",
    edition = "2018",
    rustc_flags = [
        "--cap-lints=allow",
    ],
    tags = ["cargo-raze"],
    version = "0.3.8",
    deps = [
    ],
)

# Unsupported target "bench" with type "test" omitted
# Unsupported target "map" with type "bench" omitted
# Unsupported target "map_tests" with type "test" omitted
# Unsupported target "nopanic" with type "test" omitted
