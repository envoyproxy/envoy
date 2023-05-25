load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

def envoy_jsonschema_imports():
    http_archive(
        name = "rules_proto_grpc",
        sha256 = "928e4205f701b7798ce32f3d2171c1918b363e9a600390a25c876f075f1efc0a",
        strip_prefix = "rules_proto_grpc-4.4.0",
        urls = ["https://github.com/rules-proto-grpc/rules_proto_grpc/releases/download/4.4.0/rules_proto_grpc-4.4.0.tar.gz"],
    )

    git_repository(
        name = "com_github_chrusty_protoc_gen_jsonschema",
        remote = "https://github.com/norbjd/protoc-gen-jsonschema",
        commit = "b336b0b270ef8798f8504c3fb01e09d633ddc818",
    )
