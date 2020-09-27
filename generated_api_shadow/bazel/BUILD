load("@io_bazel_rules_go//proto:compiler.bzl", "go_proto_compiler")
load("@io_bazel_rules_go//go:def.bzl", "go_binary")

licenses(["notice"])  # Apache 2

go_proto_compiler(
    name = "pgv_plugin_go",
    options = ["lang=go"],
    plugin = "@com_envoyproxy_protoc_gen_validate//:protoc-gen-validate",
    suffix = ".pb.validate.go",
    valid_archive = False,
    visibility = ["//visibility:public"],
)

go_binary(
    name = "protoc-gen-jsonschema",
    embed = [":go_default_library"],
    importpath = "github.com/chrusty/protoc-gen-jsonschema",
    visibility = ["//visibility:public"],
)
