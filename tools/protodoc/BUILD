load("@rules_python//python:defs.bzl", "py_binary")
load("@base_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package", "envoy_proto_library")
load("//tools/protodoc:protodoc.bzl", "protodoc_rule")

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

py_binary(
    name = "protodoc",
    srcs = ["protodoc.py"],
    data = [
        "//contrib:extensions_metadata.yaml",
        "//docs:protodoc_manifest.yaml",
        "//docs:v2_mapping.json",
        "//source/extensions:extensions_metadata.yaml",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":manifest_proto_py_proto",
        "//tools/api_proto_plugin",
        "//tools/config_validation:validate_fragment",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_github_cncf_udpa//xds/annotations/v3:pkg_py_proto",
        "@com_google_protobuf//:protobuf_python",
        requirement("envoy.base.utils"),
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
