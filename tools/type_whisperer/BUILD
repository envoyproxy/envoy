licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package", "envoy_proto_library")
load("//tools/type_whisperer:api_build_file.bzl", "api_build_file")
load("//tools/type_whisperer:file_descriptor_set_text.bzl", "file_descriptor_set_text")
load("//tools/type_whisperer:type_database.bzl", "type_database")
load("//tools/type_whisperer:proto_cc_source.bzl", "proto_cc_source")

envoy_package()

envoy_proto_library(
    name = "types",
    srcs = ["types.proto"],
)

py_binary(
    name = "type_whisperer",
    srcs = ["type_whisperer.py"],
    python_version = "PY3",
    visibility = ["//visibility:public"],
    deps = [
        ":types_py_proto",
        "//tools/api_proto_plugin",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_google_protobuf//:protobuf_python",
    ],
)

py_binary(
    name = "typedb_gen",
    srcs = ["typedb_gen.py"],
    python_version = "PY3",
    visibility = ["//visibility:public"],
    deps = [
        ":api_type_db_proto_py_proto",
        ":types_py_proto",
        "//tools/api_proto_plugin:utils",
        "@com_google_protobuf//:protobuf_python",
    ],
)

py_binary(
    name = "file_descriptor_set_text_gen",
    srcs = ["file_descriptor_set_text_gen.py"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_google_protobuf//:protobuf_python",
    ],
)

py_binary(
    name = "proto_cc_source_gen",
    srcs = ["proto_cc_source_gen.py"],
    visibility = ["//visibility:public"],
)

label_flag(
    name = "api_type_db_target",
    # TODO(htuch): break dependence of API type DB on docs target.
    build_setting_default = "@envoy_api_canonical//docs:protos",
    visibility = ["//visibility:public"],
)

type_database(
    name = "api_type_db",
    targets = [":api_type_db_target"],
    visibility = ["//visibility:public"],
)

file_descriptor_set_text(
    name = "all_protos_pb_text",
    deps = ["@envoy_api_canonical//:all_protos"],
)

proto_cc_source(
    name = "embedded_all_protos",
    constant = "AllProtosPbText",
    deps = [":all_protos_pb_text"],
)

proto_cc_source(
    name = "embedded_api_type_db",
    constant = "ApiTypeDbPbText",
    deps = [":api_type_db"],
)

envoy_cc_library(
    name = "api_type_db_lib",
    srcs = [
        "api_type_db.cc",
        ":embedded_all_protos",
        ":embedded_api_type_db",
    ],
    hdrs = ["api_type_db.h"],
    deps = [
        "//source/common/protobuf",
        "//tools/type_whisperer:api_type_db_proto_cc_proto",
        "@com_github_cncf_udpa//udpa/annotations:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "api_type_db_proto",
    srcs = ["api_type_db.proto"],
    deps = ["//tools/type_whisperer:types"],
)

py_binary(
    name = "proto_build_targets_gen",
    srcs = ["proto_build_targets_gen.py"],
    deps = [
        ":api_type_db_proto_py_proto",
    ],
)

api_build_file(
    name = "api_build_file",
    typedb = "//tools/type_whisperer:api_type_db",
)
