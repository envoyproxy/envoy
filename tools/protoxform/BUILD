load("@aspect_bazel_lib//lib:jq.bzl", "jq")
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_proto_library")
load("@rules_pkg//pkg:mappings.bzl", "pkg_files", "strip_prefix")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//tools/protoxform:protoxform.bzl", "protoxform_rule")

licenses(["notice"])  # Apache 2

py_binary(
    name = "protoxform",
    srcs = ["protoxform.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":options",
        ":utils",
        "//tools/api_proto_plugin",
        "//tools/type_whisperer:api_type_db_proto_py_proto",
        "@com_github_cncf_xds//udpa/annotations:pkg_py_proto",
        "@com_github_cncf_xds//xds/annotations/v3:pkg_py_proto",
        "@com_google_googleapis//google/api:annotations_py_proto",
        "@envoy_api//envoy/annotations:pkg_py_proto",
    ],
)

py_library(
    name = "options",
    srcs = ["options.py"],
    visibility = ["//visibility:public"],
)

py_library(
    name = "utils",
    srcs = ["utils.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":validate_py_pb2",
    ],
)

py_proto_library(
    name = "validate_py_pb2",
    deps = ["@com_envoyproxy_protoc_gen_validate//validate:validate_proto"],
)

protoxform_rule(
    name = "api_protoxform",
    visibility = ["//visibility:public"],
    deps = [
        "@envoy_api//versioning:active_protos",
        "@envoy_api//versioning:frozen_protos",
    ],
)

genquery(
    name = "test_proto_targets_txt.json",
    expression = "labels(srcs, labels(deps, //tools/testdata/protoxform:fix_protos))",
    scope = ["//tools/testdata/protoxform:fix_protos"],
)

jq(
    name = "test_proto_targets",
    srcs = [":test_proto_targets_txt.json"],
    out = "test_proto_targets.json",
    args = ["-sR"],
    filter = """
    split("\n") |  map(select(length>0))
    """,
    visibility = ["//visibility:public"],
)

protoxform_rule(
    name = "test_protoxform",
    visibility = ["//visibility:public"],
    deps = ["//tools/testdata/protoxform:fix_protos"],
)

pkg_files(
    name = "xformed_test_files",
    srcs = [":test_protoxform"],
    strip_prefix = strip_prefix.from_pkg(),
)

pkg_tar(
    name = "xformed_test_protos",
    srcs = [":xformed_test_files"],
    extension = "tar.gz",
    visibility = ["//visibility:public"],
)
