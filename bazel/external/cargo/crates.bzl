"""
@generated
cargo-raze crate workspace functions

DO NOT EDIT! Replaced on runs of cargo-raze
"""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")  # buildifier: disable=load
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")  # buildifier: disable=load
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")  # buildifier: disable=load

def raze_fetch_remote_crates():
    """This function defines a collection of repos and should be called in a WORKSPACE file"""
    maybe(
        http_archive,
        name = "raze__ahash__0_3_8",
        url = "https://crates.io/api/v1/crates/ahash/0.3.8/download",
        type = "tar.gz",
        sha256 = "e8fd72866655d1904d6b0997d0b07ba561047d070fbe29de039031c641b61217",
        strip_prefix = "ahash-0.3.8",
        build_file = Label("//bazel/external/cargo/remote:BUILD.ahash-0.3.8.bazel"),
    )

    maybe(
        http_archive,
        name = "raze__autocfg__1_0_1",
        url = "https://crates.io/api/v1/crates/autocfg/1.0.1/download",
        type = "tar.gz",
        sha256 = "cdb031dd78e28731d87d56cc8ffef4a8f36ca26c38fe2de700543e627f8a464a",
        strip_prefix = "autocfg-1.0.1",
        build_file = Label("//bazel/external/cargo/remote:BUILD.autocfg-1.0.1.bazel"),
    )

    maybe(
        http_archive,
        name = "raze__cfg_if__0_1_10",
        url = "https://crates.io/api/v1/crates/cfg-if/0.1.10/download",
        type = "tar.gz",
        sha256 = "4785bdd1c96b2a846b2bd7cc02e86b6b3dbf14e7e53446c4f54c92a361040822",
        strip_prefix = "cfg-if-0.1.10",
        build_file = Label("//bazel/external/cargo/remote:BUILD.cfg-if-0.1.10.bazel"),
    )

    maybe(
        http_archive,
        name = "raze__hashbrown__0_7_2",
        url = "https://crates.io/api/v1/crates/hashbrown/0.7.2/download",
        type = "tar.gz",
        sha256 = "96282e96bfcd3da0d3aa9938bedf1e50df3269b6db08b4876d2da0bb1a0841cf",
        strip_prefix = "hashbrown-0.7.2",
        build_file = Label("//bazel/external/cargo/remote:BUILD.hashbrown-0.7.2.bazel"),
    )

    maybe(
        http_archive,
        name = "raze__libc__0_2_79",
        url = "https://crates.io/api/v1/crates/libc/0.2.79/download",
        type = "tar.gz",
        sha256 = "2448f6066e80e3bfc792e9c98bf705b4b0fc6e8ef5b43e5889aff0eaa9c58743",
        strip_prefix = "libc-0.2.79",
        build_file = Label("//bazel/external/cargo/remote:BUILD.libc-0.2.79.bazel"),
    )

    maybe(
        http_archive,
        name = "raze__log__0_4_11",
        url = "https://crates.io/api/v1/crates/log/0.4.11/download",
        type = "tar.gz",
        sha256 = "4fabed175da42fed1fa0746b0ea71f412aa9d35e76e95e59b192c64b9dc2bf8b",
        strip_prefix = "log-0.4.11",
        build_file = Label("//bazel/external/cargo/remote:BUILD.log-0.4.11.bazel"),
    )

    maybe(
        http_archive,
        name = "raze__memory_units__0_4_0",
        url = "https://crates.io/api/v1/crates/memory_units/0.4.0/download",
        type = "tar.gz",
        sha256 = "8452105ba047068f40ff7093dd1d9da90898e63dd61736462e9cdda6a90ad3c3",
        strip_prefix = "memory_units-0.4.0",
        build_file = Label("//bazel/external/cargo/remote:BUILD.memory_units-0.4.0.bazel"),
    )

    maybe(
        http_archive,
        name = "raze__proxy_wasm__0_1_2",
        url = "https://crates.io/api/v1/crates/proxy-wasm/0.1.2/download",
        type = "tar.gz",
        sha256 = "23d0ac96966a8e245ee08f9ef9e46806702917f228cd82bfa46fde464c237d23",
        strip_prefix = "proxy-wasm-0.1.2",
        build_file = Label("//bazel/external/cargo/remote:BUILD.proxy-wasm-0.1.2.bazel"),
    )

    maybe(
        http_archive,
        name = "raze__wee_alloc__0_4_5",
        url = "https://crates.io/api/v1/crates/wee_alloc/0.4.5/download",
        type = "tar.gz",
        sha256 = "dbb3b5a6b2bb17cb6ad44a2e68a43e8d2722c997da10e928665c72ec6c0a0b8e",
        strip_prefix = "wee_alloc-0.4.5",
        build_file = Label("//bazel/external/cargo/remote:BUILD.wee_alloc-0.4.5.bazel"),
    )

    maybe(
        http_archive,
        name = "raze__winapi__0_3_9",
        url = "https://crates.io/api/v1/crates/winapi/0.3.9/download",
        type = "tar.gz",
        sha256 = "5c839a674fcd7a98952e593242ea400abe93992746761e38641405d28b00f419",
        strip_prefix = "winapi-0.3.9",
        build_file = Label("//bazel/external/cargo/remote:BUILD.winapi-0.3.9.bazel"),
    )

    maybe(
        http_archive,
        name = "raze__winapi_i686_pc_windows_gnu__0_4_0",
        url = "https://crates.io/api/v1/crates/winapi-i686-pc-windows-gnu/0.4.0/download",
        type = "tar.gz",
        sha256 = "ac3b87c63620426dd9b991e5ce0329eff545bccbbb34f3be09ff6fb6ab51b7b6",
        strip_prefix = "winapi-i686-pc-windows-gnu-0.4.0",
        build_file = Label("//bazel/external/cargo/remote:BUILD.winapi-i686-pc-windows-gnu-0.4.0.bazel"),
    )

    maybe(
        http_archive,
        name = "raze__winapi_x86_64_pc_windows_gnu__0_4_0",
        url = "https://crates.io/api/v1/crates/winapi-x86_64-pc-windows-gnu/0.4.0/download",
        type = "tar.gz",
        sha256 = "712e227841d057c1ee1cd2fb22fa7e5a5461ae8e48fa2ca79ec42cfc1931183f",
        strip_prefix = "winapi-x86_64-pc-windows-gnu-0.4.0",
        build_file = Label("//bazel/external/cargo/remote:BUILD.winapi-x86_64-pc-windows-gnu-0.4.0.bazel"),
    )
