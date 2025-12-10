load("@aspect_bazel_lib//lib:jq.bzl", "jq")
load("@base_pip3//:requirements.bzl", "requirement")
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_proto_library")
load("@rules_pkg//pkg:mappings.bzl", "pkg_files", "strip_prefix")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//tools/base:envoy_python.bzl", "envoy_py_data")
load("//tools/protoprint:protoprint.bzl", "protoprint_rule")

licenses(["notice"])  # Apache 2

py_binary(
    name = "protoprint",
    srcs = ["protoprint.py"],
    data = [
        "//:.clang-format",
        "//tools/clang-format",
        "//tools/type_whisperer:api_type_db.pb_text",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":utils",
        ":validate_py_pb2",
        "//tools/api_versioning:utils",
        "@envoy_repo",
        requirement("packaging"),
        "//tools/type_whisperer",
        "//tools/type_whisperer:api_type_db_proto_py_proto",
        "@com_github_cncf_xds//udpa/annotations:pkg_py_proto",
        "@com_github_cncf_xds//xds/annotations/v3:pkg_py_proto",
        "@com_google_googleapis//google/api:annotations_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@envoy_api//envoy/annotations:pkg_py_proto",
    ],
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

protoprint_rule(
    name = "protoprinted_srcs",
    deps = [
        "@envoy_api//versioning:active_protos",
        "@envoy_api//versioning:frozen_protos",
    ],
)

pkg_files(
    name = "protoprinted_files",
    srcs = [":protoprinted_srcs"],
    strip_prefix = strip_prefix.from_pkg(),
)

pkg_files(
    name = "protoprinted_files_stripped",
    srcs = [":protoprinted_files"],
    strip_prefix = "pkg",
)

pkg_tar(
    name = "protoprinted",
    srcs = [":protoprinted_files_stripped"],
    extension = "tar.gz",
    visibility = ["//visibility:public"],
)

protoprint_rule(
    name = "test_protoprinted_srcs",
    deps = ["//tools/testdata/protoxform/envoy/v2:fix_protos"],
)

pkg_files(
    name = "test_protoprinted_files_stripped",
    srcs = [":test_protoprinted_srcs"],
    strip_prefix = strip_prefix.from_pkg(),
)

pkg_tar(
    name = "test_protoprinted",
    srcs = [":test_protoprinted_files_stripped"],
    extension = "tar.gz",
)

pkg_tar(
    name = "golden_test_protos",
    srcs = ["//tools/testdata/protoxform/envoy/v2:golden_proto_files"],
    extension = "tar.gz",
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

envoy_py_data(
    name = "test_data",
    src = ":test_proto_targets",
)

py_binary(
    name = "protoprint_test",
    srcs = ["protoprint_test.py"],
    args = [
        "--formatted=$(location :test_protoprinted)",
        "--golden=$(location :golden_test_protos)",
    ],
    data = [
        ":golden_test_protos",
        ":test_protoprinted",
    ],
    deps = [
        ":test_data",
        "//tools:run_command",
    ],
)
