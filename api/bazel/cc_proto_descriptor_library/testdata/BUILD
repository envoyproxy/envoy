load("//bazel/cc_proto_descriptor_library:builddefs.bzl", "cc_proto_descriptor_library")

licenses(["notice"])  # Apache 2

# Tests for go/cc_proto_descriptor_library.
# These need to be in a separate directory to test visibility restrictions.

proto_library(
    name = "test_proto",
    testonly = True,
    srcs = ["test.proto"],
    visibility = ["//visibility:private"],
    deps = [
        "@com_google_protobuf//:any_proto",
    ],
)

proto_library(
    name = "test1_proto",
    testonly = True,
    srcs = ["test1.proto"],
    visibility = ["//visibility:private"],
    deps = [
        ":test_proto",
        "@com_google_protobuf//:any_proto",
    ],
)

proto_library(
    name = "test_extension_proto",
    testonly = True,
    # Purprosely a dash to make sure they're handled properly in filenames
    srcs = ["test-extension.proto"],
    visibility = ["//visibility:private"],
    deps = [
        ":test_proto",
        "@com_google_protobuf//:any_proto",
    ],
)

cc_proto_library(
    name = "test_cc_proto",
    testonly = True,
    visibility = ["//visibility:private"],
    deps = ["test_proto"],
)

cc_proto_library(
    name = "test1_cc_proto",
    testonly = True,
    visibility = ["//visibility:private"],
    deps = ["test1_proto"],
)

cc_proto_library(
    name = "test_extension_cc_proto",
    testonly = True,
    visibility = ["//visibility:private"],
    deps = ["test_extension_proto"],
)

cc_proto_descriptor_library(
    name = "test_descriptors",
    testonly = True,
    visibility = ["//visibility:private"],
    deps = [
        ":test_proto",
    ],
)

cc_proto_descriptor_library(
    name = "test1_descriptors",
    testonly = True,
    visibility = ["//visibility:private"],
    deps = [
        ":test1_proto",
    ],
)

cc_proto_descriptor_library(
    name = "test_extension_descriptors",
    testonly = True,
    visibility = ["//visibility:private"],
    deps = [
        ":test_extension_proto",
    ],
)

cc_test(
    name = "text_format_transcoder_test",
    srcs = [
        "text_format_transcoder_test.cc",
    ],
    visibility = ["//visibility:private"],
    deps = [
        "test1_descriptors",
        "test_cc_proto",
        "test_descriptors",
        "test_extension_cc_proto",
        "test_extension_descriptors",
        "//bazel/cc_proto_descriptor_library:text_format_transcoder",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "global_fallback_test",
    srcs = [
        "global_fallback_test.cc",
    ],
    visibility = ["//visibility:private"],
    deps = [
        "test_cc_proto",
        "//bazel/cc_proto_descriptor_library:text_format_transcoder",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "create_dynamic_message_test",
    srcs = [
        "create_dynamic_message_test.cc",
    ],
    visibility = ["//visibility:private"],
    deps = [
        "test_cc_proto",
        "test_descriptors",
        "//bazel/cc_proto_descriptor_library:create_dynamic_message",
        "//bazel/cc_proto_descriptor_library:text_format_transcoder",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
    ],
)
