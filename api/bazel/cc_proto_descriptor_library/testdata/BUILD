load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
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
        "@protobuf//:any_proto",
    ],
)

proto_library(
    name = "test1_proto",
    testonly = True,
    srcs = ["test1.proto"],
    visibility = ["//visibility:private"],
    deps = [
        ":test_proto",
        "@protobuf//:any_proto",
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
        "@protobuf//:any_proto",
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

proto_library(
    name = "test_raw_bytes_proto",
    testonly = True,
    srcs = ["test-raw-bytes.proto"],
    visibility = ["//visibility:private"],
)

cc_proto_library(
    name = "test_raw_bytes_cc_proto",
    testonly = True,
    visibility = ["//visibility:private"],
    deps = [":test_raw_bytes_proto"],
)

# Generates a proto whose serialized descriptor is larger than 64 KiB, which the generator embeds
# as a char array rather than a string literal.
genrule(
    name = "gen_test_large_proto",
    testonly = True,
    srcs = ["test_large.proto.prefix"],
    outs = ["test_large.proto"],
    cmd = """(
        cat $<
        for i in $$(seq 100 1299); do
          echo "  optional string field_with_a_long_name_to_pad_the_descriptor_past_sixty_four_kib$$i = $$i;"
        done
        echo "}"
    ) > $@""",
)

proto_library(
    name = "test_large_proto",
    testonly = True,
    srcs = ["test_large.proto"],
    visibility = ["//visibility:private"],
)

cc_proto_library(
    name = "test_large_cc_proto",
    testonly = True,
    visibility = ["//visibility:private"],
    deps = [":test_large_proto"],
)

cc_proto_descriptor_library(
    name = "test_raw_bytes_descriptors",
    testonly = True,
    visibility = ["//visibility:private"],
    deps = [
        ":test_raw_bytes_proto",
    ],
)

cc_proto_descriptor_library(
    name = "test_large_descriptors",
    testonly = True,
    visibility = ["//visibility:private"],
    deps = [
        ":test_large_proto",
    ],
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
        "@googletest//:gtest_main",
        "@protobuf",
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
        "@googletest//:gtest_main",
        "@protobuf",
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
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)

cc_test(
    name = "file_descriptor_generator_test",
    srcs = [
        "file_descriptor_generator_test.cc",
    ],
    visibility = ["//visibility:private"],
    deps = [
        "test_large_cc_proto",
        "test_large_descriptors",
        "test_raw_bytes_cc_proto",
        "test_raw_bytes_descriptors",
        "//bazel/cc_proto_descriptor_library:file_descriptor_info",
        "//bazel/cc_proto_descriptor_library:text_format_transcoder",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)
