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
        name = "raze__protobuf__2_23_0",
        url = "https://crates.io/api/v1/crates/protobuf/2.23.0/download",
        type = "tar.gz",
        sha256 = "45604fc7a88158e7d514d8e22e14ac746081e7a70d7690074dd0029ee37458d6",
        strip_prefix = "protobuf-2.23.0",
        build_file = Label("//bazel/external/cargo/remote:BUILD.protobuf-2.23.0.bazel"),
    )
