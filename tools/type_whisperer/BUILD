licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package", "envoy_proto_library")
load("//tools/type_whisperer:type_database.bzl", "type_database")
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

label_flag(
    name = "api_type_db_target",
    # TODO(htuch): break dependence of API type DB on docs target.
    build_setting_default = "@envoy_api//docs:protos",
    visibility = ["//visibility:public"],
)

type_database(
    name = "api_type_db",
    targets = [":api_type_db_target"],
    visibility = ["//visibility:public"],
)

# Pack API type database text file into a char* string that can be referenced
# at the C++ level.
genrule(
    name = "api_type_db_genrule",
    srcs = [":api_type_db"],
    outs = ["api_type_db_def.generated.cc"],
    cmd = "(echo 'namespace Envoy { namespace Tools { namespace TypeWhisperer " +
          "{ const char* ApiTypeDbPbText = R\"EOF('; cat $(SRCS); echo ')EOF\";}}}') > $@",
)

envoy_cc_library(
    name = "api_type_db_lib",
    srcs = [
        "api_type_db.cc",
        "api_type_db_def.generated.cc",
    ],
    hdrs = ["api_type_db.h"],
    deps = [
        "//source/common/protobuf",
        "//tools/type_whisperer:api_type_db_proto_cc_proto",
    ],
)

envoy_proto_library(
    name = "api_type_db_proto",
    srcs = ["api_type_db.proto"],
)
