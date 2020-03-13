licenses(["notice"])  # Apache 2

load("@rules_python//python:defs.bzl", "py_library")
load("//tools/type_whisperer:type_database.bzl", "type_database")

py_library(
    name = "api_proto_plugin",
    srcs = [
        "annotations.py",
        "plugin.py",
        "traverse.py",
        "type_context.py",
        "visitor.py",
    ],
    srcs_version = "PY3",
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_protobuf//:protobuf_python",
    ],
)

py_library(
    name = "utils",
    srcs = ["utils.py"],
    srcs_version = "PY3",
    visibility = ["//visibility:public"],
)

label_flag(
    name = "default_type_db_target",
    # While this is not completely empty but type_db_gen generates nothing on this target.
    build_setting_default = "@com_google_protobuf//:empty_proto",
    visibility = ["//visibility:public"],
)

type_database(
    name = "default_type_db",
    targets = [":default_type_db_target"],
    visibility = ["//visibility:public"],
)
