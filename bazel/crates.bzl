"""
cargo-raze crate workspace functions

DO NOT EDIT! Replaced on runs of cargo-raze
"""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

def _new_http_archive(name, **kwargs):
    if not native.existing_rule(name):
        http_archive(name = name, **kwargs)

def _new_git_repository(name, **kwargs):
    if not native.existing_rule(name):
        new_git_repository(name = name, **kwargs)

def raze_fetch_remote_crates():
    _new_http_archive(
        name = "raze__ahash__0_3_8",
        url = "https://crates-io.s3-us-west-1.amazonaws.com/crates/ahash/ahash-0.3.8.crate",
        type = "tar.gz",
        strip_prefix = "ahash-0.3.8",
        build_file = Label("//bazel/external/cargo/remote:ahash-0.3.8.BUILD"),
    )

    _new_http_archive(
        name = "raze__autocfg__1_0_0",
        url = "https://crates-io.s3-us-west-1.amazonaws.com/crates/autocfg/autocfg-1.0.0.crate",
        type = "tar.gz",
        strip_prefix = "autocfg-1.0.0",
        build_file = Label("//bazel/external/cargo/remote:autocfg-1.0.0.BUILD"),
    )

    _new_http_archive(
        name = "raze__cfg_if__0_1_10",
        url = "https://crates-io.s3-us-west-1.amazonaws.com/crates/cfg-if/cfg-if-0.1.10.crate",
        type = "tar.gz",
        strip_prefix = "cfg-if-0.1.10",
        build_file = Label("//bazel/external/cargo/remote:cfg-if-0.1.10.BUILD"),
    )

    _new_http_archive(
        name = "raze__hashbrown__0_7_2",
        url = "https://crates-io.s3-us-west-1.amazonaws.com/crates/hashbrown/hashbrown-0.7.2.crate",
        type = "tar.gz",
        strip_prefix = "hashbrown-0.7.2",
        build_file = Label("//bazel/external/cargo/remote:hashbrown-0.7.2.BUILD"),
    )

    _new_http_archive(
        name = "raze__libc__0_2_74",
        url = "https://crates-io.s3-us-west-1.amazonaws.com/crates/libc/libc-0.2.74.crate",
        type = "tar.gz",
        strip_prefix = "libc-0.2.74",
        build_file = Label("//bazel/external/cargo/remote:libc-0.2.74.BUILD"),
    )

    _new_http_archive(
        name = "raze__log__0_4_11",
        url = "https://crates-io.s3-us-west-1.amazonaws.com/crates/log/log-0.4.11.crate",
        type = "tar.gz",
        strip_prefix = "log-0.4.11",
        build_file = Label("//bazel/external/cargo/remote:log-0.4.11.BUILD"),
    )

    _new_http_archive(
        name = "raze__memory_units__0_4_0",
        url = "https://crates-io.s3-us-west-1.amazonaws.com/crates/memory_units/memory_units-0.4.0.crate",
        type = "tar.gz",
        strip_prefix = "memory_units-0.4.0",
        build_file = Label("//bazel/external/cargo/remote:memory_units-0.4.0.BUILD"),
    )

    _new_http_archive(
        name = "raze__proxy_wasm__0_1_2",
        url = "https://crates-io.s3-us-west-1.amazonaws.com/crates/proxy-wasm/proxy-wasm-0.1.2.crate",
        type = "tar.gz",
        strip_prefix = "proxy-wasm-0.1.2",
        build_file = Label("//bazel/external/cargo/remote:proxy-wasm-0.1.2.BUILD"),
    )

    _new_http_archive(
        name = "raze__wee_alloc__0_4_5",
        url = "https://crates-io.s3-us-west-1.amazonaws.com/crates/wee_alloc/wee_alloc-0.4.5.crate",
        type = "tar.gz",
        strip_prefix = "wee_alloc-0.4.5",
        build_file = Label("//bazel/external/cargo/remote:wee_alloc-0.4.5.BUILD"),
    )

    _new_http_archive(
        name = "raze__winapi__0_3_9",
        url = "https://crates-io.s3-us-west-1.amazonaws.com/crates/winapi/winapi-0.3.9.crate",
        type = "tar.gz",
        strip_prefix = "winapi-0.3.9",
        build_file = Label("//bazel/external/cargo/remote:winapi-0.3.9.BUILD"),
    )

    _new_http_archive(
        name = "raze__winapi_i686_pc_windows_gnu__0_4_0",
        url = "https://crates-io.s3-us-west-1.amazonaws.com/crates/winapi-i686-pc-windows-gnu/winapi-i686-pc-windows-gnu-0.4.0.crate",
        type = "tar.gz",
        strip_prefix = "winapi-i686-pc-windows-gnu-0.4.0",
        build_file = Label("//bazel/external/cargo/remote:winapi-i686-pc-windows-gnu-0.4.0.BUILD"),
    )

    _new_http_archive(
        name = "raze__winapi_x86_64_pc_windows_gnu__0_4_0",
        url = "https://crates-io.s3-us-west-1.amazonaws.com/crates/winapi-x86_64-pc-windows-gnu/winapi-x86_64-pc-windows-gnu-0.4.0.crate",
        type = "tar.gz",
        strip_prefix = "winapi-x86_64-pc-windows-gnu-0.4.0",
        build_file = Label("//bazel/external/cargo/remote:winapi-x86_64-pc-windows-gnu-0.4.0.BUILD"),
    )
