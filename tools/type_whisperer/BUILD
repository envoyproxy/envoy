licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_package", "envoy_proto_library")
load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")

envoy_package()

py_proto_library(
    name = "types_py_proto",
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

envoy_proto_library(
    name = "api_type_db_proto",
    srcs = ["api_type_db.proto"],
)
