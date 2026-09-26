load("@aspect_bazel_lib//lib:jq.bzl", "jq")
load("@envoy_toolshed//:macros.bzl", "json_data")
load("@rules_go//proto:compiler.bzl", "go_proto_compiler")
load("@rules_java//toolchains:default_java_toolchain.bzl", "default_java_toolchain")
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

# envoy_api needs the same hermetic Java toolchain arrangement as Envoy itself because its proto
# build graph exposes Java artifacts independently of the main @envoy module.
default_java_toolchain(
    name = "envoy_api_java_toolchain",
    java_runtime = "@rules_java//toolchains:remotejdk_25",
    javabuilder_jvm_opts = ["--sun-misc-unsafe-memory-access=allow"],
    source_version = "11",
    target_version = "11",
)

toolchain(
    name = "envoy_api_java_runtime_toolchain",
    toolchain = "@rules_java//toolchains:remotejdk_25",
    toolchain_type = "@bazel_tools//tools/jdk:runtime_toolchain_type",
)

toolchain(
    name = "envoy_api_java_bootstrap_runtime_toolchain",
    toolchain = "@rules_java//toolchains:remotejdk_25",
    toolchain_type = "@bazel_tools//tools/jdk:bootstrap_runtime_toolchain_type",
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
