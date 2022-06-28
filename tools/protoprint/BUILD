load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@rules_python//python:defs.bzl", "py_binary")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("//tools/base:envoy_python.bzl", "envoy_py_data")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "protoprint",
    srcs = ["protoprint.py"],
    data = [
        "//:.clang-format",
        "//:API_VERSION.txt",
        "//tools/type_whisperer:api_type_db.pb_text",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//tools/api_versioning:utils",
        "//tools/protoxform:options",
        "//tools/protoxform:utils",
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

pkg_tar(
    name = "golden_test_protos",
    srcs = ["//tools/testdata/protoxform/envoy/v2:golden_proto_files"],
)

envoy_py_data(
    name = "test_data",
    src = "//tools/protoxform:test_proto_targets",
)

py_binary(
    name = "protoprint_test",
    srcs = ["protoprint_test.py"],
    args = [
        "$(location :protoprint)",
        "$(location //tools/type_whisperer:api_type_db.pb_text)",
        "$(location //tools/protoxform:xformed_test_protos)",
        "$(location :golden_test_protos)",
    ],
    data = [
        ":golden_test_protos",
        "//tools/protoxform:xformed_test_protos",
        "//tools/type_whisperer:api_type_db.pb_text",
    ],
    deps = [
        ":protoprint",
        ":test_data",
        "//tools:run_command",
        "@envoy_repo",
    ],
)
