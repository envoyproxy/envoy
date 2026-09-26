load("@aspect_bazel_lib//lib:jq.bzl", "jq")
load("@envoy_toolshed//:macros.bzl", "json_data")
load("@envoy_toolshed//dependency:macros.bzl", "module_deps_json", "module_updater", "registry_updater")
load("@rules_go//proto:compiler.bzl", "go_proto_compiler")
load(
    ":external_proto_deps.bzl",
    "EXTERNAL_PROTO_CC_BAZEL_DEP_MAP",
    "EXTERNAL_PROTO_GO_BAZEL_DEP_MAP",
    "EXTERNAL_PROTO_IMPORT_BAZEL_DEP_MAP",
)

licenses(["notice"])  # Apache 2

exports_files([
    "extensions.bzl",
    "utils.bzl",
    "deps.yaml",
])

module_deps_json(
    name = "module_deps",
    lockfile = "//:MODULE.bazel.lock",
    module_file = "//:MODULE.bazel",
)

module_updater(
    name = "update_module",
    bazelrc = "//:.bazelrc",
    dependencies = ":module_deps",
    module_file = "//:MODULE.bazel",
)

registry_updater(
    name = "update_registry",
    bazelrc = "//:.bazelrc",
)

go_proto_compiler(
    name = "vtprotobuf_plugin_go",
    options = [
        "features=marshal_strict+size",
        "buildTag=vtprotobuf",
    ],
    plugin = "@com_github_planetscale_vtprotobuf//cmd/protoc-gen-go-vtproto",
    suffix = "_vtproto.pb.go",
    valid_archive = False,
    visibility = ["//visibility:public"],
)

json_data(
    name = "external_proto_deps",
    data = dict(
        cc = EXTERNAL_PROTO_CC_BAZEL_DEP_MAP,
        go = EXTERNAL_PROTO_GO_BAZEL_DEP_MAP,
        imports = EXTERNAL_PROTO_IMPORT_BAZEL_DEP_MAP,
    ),
)
