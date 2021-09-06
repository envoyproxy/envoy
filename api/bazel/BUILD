load("@io_bazel_rules_go//proto:compiler.bzl", "go_proto_compiler")
load("@envoy//bazel:utils.bzl", "write_json")
load("@envoy//tools/base:envoy_python.bzl", "py_data")
load(":repository_locations.bzl", "REPOSITORY_LOCATIONS_SPEC")

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

write_json(
    name = "repository_locations",
    data = REPOSITORY_LOCATIONS_SPEC,
)

py_data(
    name = "repository_locations",
    filters = ["@envoy//tools/dependency:interpolate_repository_locations"],
    source = ":repository_locations.json",
)
