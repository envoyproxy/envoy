"""
@generated
cargo-raze generated Bazel file.

DO NOT EDIT! Replaced on runs of cargo-raze
"""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")  # buildifier: disable=load
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")  # buildifier: disable=load
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")  # buildifier: disable=load

def raze_fetch_remote_crates():
    """This function defines a collection of repos and should be called in a WORKSPACE file"""
    maybe(
        http_archive,
        name = "raze__protobuf__2_24_1",
        url = "https://crates.io/api/v1/crates/protobuf/2.24.1/download",
        type = "tar.gz",
        sha256 = "db50e77ae196458ccd3dc58a31ea1a90b0698ab1b7928d89f644c25d72070267",
        strip_prefix = "protobuf-2.24.1",
        build_file = Label("//bazel/external/cargo/remote:BUILD.protobuf-2.24.1.bazel"),
    )
