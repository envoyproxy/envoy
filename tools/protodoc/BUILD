load("@rules_python//python:defs.bzl", "py_binary")
load("@base_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package", "envoy_proto_library")
load("//tools/protodoc:protodoc.bzl", "protodoc_rule")
load("//tools/base:envoy_python.bzl", "envoy_py_data")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "generate_empty",
    srcs = ["generate_empty.py"],
    visibility = ["//visibility:public"],
    deps = [":protodoc"],
)

envoy_proto_library(
    name = "manifest_proto",
    srcs = ["manifest.proto"],
)

envoy_py_data(
    name = "extensions_db",
    src = "//source/extensions:extensions_metadata.yaml",
)

envoy_py_data(
    name = "contrib_extensions_db",
    src = "//contrib:extensions_metadata.yaml",
)

envoy_py_data(
    name = "protodoc_manifest_untyped",
    src = "//docs:protodoc_manifest.yaml",
)

py_binary(
    name = "protodoc",
    srcs = ["protodoc.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":contrib_extensions_db",
        ":extensions_db",
        ":manifest_proto_py_proto",
        ":protodoc_manifest_untyped",
        "//tools/api_proto_plugin",
        "//tools/config_validation:validate_fragment",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_github_cncf_udpa//xds/annotations/v3:pkg_py_proto",
        "@com_google_protobuf//:protobuf_python",
        requirement("envoy.code.check"),
        requirement("Jinja2"),
    ],
)

protodoc_rule(
    name = "api_v3_protodoc",
    deps = ["@envoy_api//:v3_protos"],
)

protodoc_rule(
    name = "xds_protodoc",
    deps = ["@envoy_api//:xds_protos"],
)
