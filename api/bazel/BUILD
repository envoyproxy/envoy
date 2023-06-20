load("@io_bazel_rules_go//proto:compiler.bzl", "go_proto_compiler")
load(":utils.bzl", "json_data")
load(":repository_locations.bzl", "REPOSITORY_LOCATIONS_SPEC")
load(":repository_locations_utils.bzl", "load_repository_locations_spec")
load(
    ":external_proto_deps.bzl",
    "EXTERNAL_PROTO_CC_BAZEL_DEP_MAP",
    "EXTERNAL_PROTO_GO_BAZEL_DEP_MAP",
    "EXTERNAL_PROTO_IMPORT_BAZEL_DEP_MAP",
    "EXTERNAL_PROTO_PY_BAZEL_DEP_MAP",
)

licenses(["notice"])  # Apache 2

exports_files([
    "repository_locations.bzl",
    "repository_locations_utils.bzl",
    "utils.bzl",
])

go_proto_compiler(
    name = "pgv_plugin_go",
    options = ["lang=go"],
    plugin = "@com_envoyproxy_protoc_gen_validate//:protoc-gen-validate",
    suffix = ".pb.validate.go",
    valid_archive = False,
    visibility = ["//visibility:public"],
)

json_data(
    name = "repository_locations",
    data = load_repository_locations_spec(REPOSITORY_LOCATIONS_SPEC),
)

json_data(
    name = "external_proto_deps",
    data = dict(
        cc = EXTERNAL_PROTO_CC_BAZEL_DEP_MAP,
        go = EXTERNAL_PROTO_GO_BAZEL_DEP_MAP,
        imports = EXTERNAL_PROTO_IMPORT_BAZEL_DEP_MAP,
        py = EXTERNAL_PROTO_PY_BAZEL_DEP_MAP,
    ),
)
