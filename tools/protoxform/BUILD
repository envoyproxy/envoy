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
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_github_cncf_udpa//xds/annotations/v3:pkg_py_proto",
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
)

protoxform_rule(
    name = "api_protoxform",
    visibility = ["//visibility:public"],
    deps = [
        "@envoy_api//versioning:active_protos",
        "@envoy_api//versioning:frozen_protos",
    ],
)

protoxform_rule(
    name = "test_protoxform",
    visibility = ["//visibility:public"],
    deps = ["//tools/testdata/protoxform:fix_protos"],
)
