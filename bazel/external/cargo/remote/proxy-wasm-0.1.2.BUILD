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
    "notice",  # Apache-2.0 from expression "Apache-2.0"
])

# Unsupported target "hello_world" with type "example" omitted
# Unsupported target "http_auth_random" with type "example" omitted
# Unsupported target "http_body" with type "example" omitted
# Unsupported target "http_headers" with type "example" omitted

rust_library(
    name = "proxy_wasm",
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
    version = "0.1.2",
    deps = [
        "@raze__hashbrown__0_7_2//:hashbrown",
        "@raze__log__0_4_11//:log",
        "@raze__wee_alloc__0_4_5//:wee_alloc",
    ],
)
