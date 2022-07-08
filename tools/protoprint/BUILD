load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary")
load("@rules_pkg//pkg:mappings.bzl", "pkg_files", "strip_prefix")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("//tools/base:envoy_python.bzl", "envoy_py_data")
load("//tools/protoprint:protoprint.bzl", "protoprint_rule")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "protoprint",
    srcs = ["protoprint.py"],
    data = [
        "//:.clang-format",
        "//tools/type_whisperer:api_type_db.pb_text",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//tools/api_versioning:utils",
        "//tools/protoxform:options",
        "//tools/protoxform:utils",
        "@envoy_repo",
        requirement("packaging"),
        "//tools/type_whisperer",
        "//tools/type_whisperer:api_type_db_proto_py_proto",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_github_cncf_udpa//xds/annotations/v3:pkg_py_proto",
        "@com_google_googleapis//google/api:annotations_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@envoy_api//envoy/annotations:pkg_py_proto",
    ],
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
)

protoprint_rule(
    name = "test_protoprinted_srcs",
    deps = ["//tools/protoxform:test_protoxform"],
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

envoy_py_data(
    name = "test_data",
    src = "//tools/protoxform:test_proto_targets",
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
        "@envoy_repo",
    ],
)
