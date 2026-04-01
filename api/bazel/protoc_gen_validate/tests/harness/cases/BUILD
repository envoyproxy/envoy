load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@rules_java//java:defs.bzl", "java_proto_library")
load(
    "//bazel:pgv_proto_library.bzl",
    "pgv_cc_proto_library",
    "pgv_go_proto_library",
    "pgv_java_proto_library",
)

# gazelle:go_generate_proto false

proto_library(
    name = "cases_proto",
    srcs = [
        "bool.proto",
        "bytes.proto",
        "enums.proto",
        "filename-with-dash.proto",
        "kitchen_sink.proto",
        "maps.proto",
        "messages.proto",
        "numbers.proto",
        "oneofs.proto",
        "repeated.proto",
        "strings.proto",
        "subdirectory/in_subdirectory.proto",
        "wkt_any.proto",
        "wkt_duration.proto",
        "wkt_nested.proto",
        "wkt_timestamp.proto",
        "wkt_wrappers.proto",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//tests/harness/cases/other_package:embed_proto",
        "//tests/harness/cases/sort:sort_proto",
        "//tests/harness/cases/yet_another_package:embed_proto",
        "//validate:validate_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

pgv_go_proto_library(
    name = "go",
    importpath = "github.com/envoyproxy/protoc-gen-validate/tests/harness/cases/go",
    proto = ":cases_proto",
    deps = [
        "//tests/harness/cases/other_package:go",
        "//tests/harness/cases/sort:go",
        "//tests/harness/cases/yet_another_package:go",
        "@org_golang_google_protobuf//types/known/anypb:go_default_library",
        "@org_golang_google_protobuf//types/known/durationpb:go_default_library",
        "@org_golang_google_protobuf//types/known/timestamppb:go_default_library",
        "@org_golang_google_protobuf//types/known/wrapperspb:go_default_library",
    ],
)

pgv_cc_proto_library(
    name = "cc",
    cc_deps = [
        "//tests/harness/cases/other_package:cc",
        "//tests/harness/cases/sort:cc",
        "//tests/harness/cases/yet_another_package:cc",
    ],
    visibility = ["//tests:__subpackages__"],
    deps = [":cases_proto"],
)

java_proto_library(
    name = "cases_java_proto",
    visibility = ["//visibility:public"],
    deps = [":cases_proto"],
)

pgv_java_proto_library(
    name = "java",
    java_deps = [
        ":cases_java_proto",
        "//tests/harness/cases/other_package:java",
        "//tests/harness/cases/yet_another_package:java",
        "//tests/harness/cases/sort:java",
    ],
    visibility = ["//visibility:public"],
    deps = [":cases_proto"],
)

py_proto_library(
    name = "cases_py_proto",
    visibility = ["//visibility:public"],
    deps = [":cases_proto"],
)
