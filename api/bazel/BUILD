load("@io_bazel_rules_go//proto:compiler.bzl", "go_proto_compiler")

licenses(["notice"])  # Apache 2

exports_files([
    "repository_locations.bzl",
    "repository_locations_utils.bzl",
])

go_proto_compiler(
    name = "pgv_plugin_go",
    options = ["lang=go"],
    plugin = "@com_envoyproxy_protoc_gen_validate//:protoc-gen-validate",
    suffix = ".pb.validate.go",
    valid_archive = False,
    visibility = ["//visibility:public"],
)
