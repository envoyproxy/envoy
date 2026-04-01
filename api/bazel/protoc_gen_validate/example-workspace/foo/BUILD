load("@com_envoyproxy_protoc_gen_validate//bazel:pgv_proto_library.bzl", "pgv_cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")

package(
    default_visibility = ["//visibility:public"],
)

proto_library(
    name = "bar_proto",
    srcs = ["bar.proto"],
    deps = ["@com_envoyproxy_protoc_gen_validate//validate:validate_proto"],
)

pgv_cc_proto_library(
    name = "bar_cc_proto",
    deps = [":bar_proto"],
)

proto_library(
    name = "baz_proto",
    srcs = ["subdir_without_package/baz.proto"],
    deps = ["@com_envoyproxy_protoc_gen_validate//validate:validate_proto"],
)

pgv_cc_proto_library(
    name = "baz_cc_proto",
    deps = [":baz_proto"],
)

cc_test(
    name = "baz_cc_test",
    srcs = ["baz_cc_test.cc"],
    deps = [":baz_cc_proto"],
)

py_proto_library(
    name = "bar_py_proto",
    deps = ["bar_proto"],
)
