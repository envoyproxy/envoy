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

# Unsupported target "bench" with type "bench" omitted
# Unsupported target "build-script-build" with type "custom-build" omitted

rust_library(
    name = "hashbrown",
    srcs = glob(["**/*.rs"]),
    crate_features = [
        "ahash",
        "inline-more",
    ],
    crate_root = "src/lib.rs",
    crate_type = "lib",
    edition = "2018",
    rustc_flags = [
        "--cap-lints=allow",
    ],
    tags = ["cargo-raze"],
    version = "0.7.2",
    deps = [
        "@raze__ahash__0_3_8//:ahash",
    ],
)

# Unsupported target "hasher" with type "test" omitted
# Unsupported target "rayon" with type "test" omitted
# Unsupported target "serde" with type "test" omitted
# Unsupported target "set" with type "test" omitted
