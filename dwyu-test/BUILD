# This BUILD file shows how to use protobuf with bazel. Before you can use
# proto_library/<lang>_proto_library rules in a BUILD file, you need to
# include protobuf repo as remote repositories in your WORKSPACE file. See
# the WORKSPACE file in the same directory with this BUILD file for an
# example.

load("@bazel_skylib//rules:build_test.bzl", "build_test")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:java_lite_proto_library.bzl", "java_lite_proto_library")
load("@com_google_protobuf//bazel:java_proto_library.bzl", "java_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_java//java:java_binary.bzl", "java_binary")
load("@rules_pkg//pkg:mappings.bzl", "pkg_files", "strip_prefix")
load("@rules_python//python:py_binary.bzl", "py_binary")

# cc_proto_library is intentionally not loaded, to test Bazel's built-in implementation
# against Protobuf's implementation (already used building protoc)

# For each .proto file, a proto_library target should be defined. This target
# is not bound to any particular language. Instead, it defines the dependency
# graph of the .proto files (i.e., proto imports) and serves as the provider
# of .proto source files to the protocol compiler.
#
# Remote repository "protobuf" must be defined to use this rule.
proto_library(
    name = "addressbook_proto",
    srcs = ["addressbook.proto"],
    deps = ["@com_google_protobuf//:timestamp_proto"],
)

# The cc_proto_library rule generates C++ code for a proto_library rule. It
# must have exactly one proto_library dependency. If you want to use multiple
# proto_library targets, create a separate cc_proto_library target for each
# of them.
#
# Remote repository "com_google_protobuf_cc" must be defined to use this rule.
cc_proto_library(
    name = "addressbook_cc_proto",
    deps = [":addressbook_proto"],
)

# cc_library/cc_binary targets can depend on cc_proto_library targets.
cc_binary(
    name = "add_person_cpp",
    srcs = ["add_person.cc"],
    deps = [
        ":addressbook_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//src/google/protobuf/util:time_util",
    ],
)
