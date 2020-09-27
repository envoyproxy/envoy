load("@rules_python//python:defs.bzl", "py_binary")
load("@protoschema_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package", "envoy_proto_library")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "manifest_proto",
    srcs = ["manifest.proto"],
)

py_binary(
    name = "protoschema",
    srcs = ["protoschema.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":manifest_proto_py_proto",
        "//tools/api_proto_plugin",
        "//tools/config_validation:validate_fragment",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_google_protobuf//:protobuf_python",
        requirement("PyYAML"),
    ],
)
