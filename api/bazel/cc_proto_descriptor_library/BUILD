licenses(["notice"])  # Apache 2

cc_library(
    name = "file_descriptor_info",
    hdrs = ["file_descriptor_info.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "text_format_transcoder",
    srcs = [
        "text_format_transcoder.cc",
    ],
    hdrs = [
        "create_dynamic_message.h",
        "text_format_transcoder.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":file_descriptor_info",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "file_descriptor_generator_lib",
    srcs = ["file_descriptor_generator.cc"],
    hdrs = ["file_descriptor_generator.h"],
    deps = [
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//src/google/protobuf/compiler:code_generator",
    ],
)

cc_binary(
    name = "file_descriptor_generator",
    srcs = ["file_descriptor_generator_main.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":file_descriptor_generator_lib",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protoc_lib",
        "@com_google_protobuf//src/google/protobuf/compiler:code_generator",
    ],
)

cc_library(
    name = "create_dynamic_message",
    srcs = ["create_dynamic_message.cc"],
    hdrs = ["create_dynamic_message.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":text_format_transcoder",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
    ],
)
