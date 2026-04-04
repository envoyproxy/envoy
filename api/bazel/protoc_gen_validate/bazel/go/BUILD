"""Define our protoc plugin in a rule that can be provided to go_proto_library#compilers

This is in a separate BUILD file from /bazel/BUILD to avoid a dependency on rules_go for all users who load from it.

If you use Gazelle, you can use a directive so that the pgv_plugin_go is automatically added to generated go_proto_library rules.
See https://github.com/bazelbuild/bazel-gazelle#directives

For example, in some/BUILD.bazel:
# gazelle:go_grpc_compilers @com_envoyproxy_protoc_gen_validate//bazel/go:pgv_plugin_go, @io_bazel_rules_go//proto:go_grpc
"""

load("@io_bazel_rules_go//proto:compiler.bzl", "go_proto_compiler")

go_proto_compiler(
    name = "pgv_plugin_go",
    options = ["lang=go"],
    plugin = "@com_envoyproxy_protoc_gen_validate//:protoc-gen-validate",
    suffix = ".pb.validate.go",
    valid_archive = False,
    visibility = ["//visibility:public"],
)
