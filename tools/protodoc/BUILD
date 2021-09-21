load("@rules_python//python:defs.bzl", "py_binary")
load("@base_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package", "envoy_proto_library")
load("//tools/protodoc:protodoc.bzl", "protodoc_rule")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "manifest_proto",
    srcs = ["manifest.proto"],
)

py_binary(
    name = "protodoc",
    srcs = ["protodoc.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//tools/api_proto_plugin",
        "//tools/config_validation:validate_fragment",
        "//tools/docs:utils",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_google_protobuf//:protobuf_python",
        requirement("envoy.base.utils"),
        requirement("Jinja2"),
    ],
)

protodoc_rule(
    name = "api_v3_protodoc",
    deps = ["@envoy_api//:v3_protos"],
)
