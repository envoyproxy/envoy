load("//bazel/cc_proto_descriptor_library:builddefs.bzl", "cc_proto_descriptor_library")

licenses(["notice"])  # Apache 2

# Tests for go/cc_proto_descriptor_library.
# These need to be in a separate directory to test visibility restrictions.

proto_library(
    name = "all_proto",
    testonly = True,
    srcs = [
        "ancestor.proto",
        "base_with_any.proto",
        "derived.proto",
    ],
    visibility = ["//visibility:private"],
    deps = [
        "@com_google_protobuf//:any_proto",
    ],
)

cc_proto_library(
    name = "all_cc_proto",
    testonly = True,
    visibility = ["//visibility:private"],
    deps = ["all_proto"],
)

cc_proto_descriptor_library(
    name = "all_cc_proto_descriptor",
    testonly = True,
    visibility = ["//visibility:private"],
    deps = [
        ":all_proto",
    ],
)

cc_test(
    name = "repro_test",
    srcs = [
        "repro_test.cc",
    ],
    visibility = ["//visibility:private"],
    deps = [
        "all_cc_proto",
        "all_cc_proto_descriptor",
        "//bazel/cc_proto_descriptor_library:text_format_transcoder",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
    ],
)
