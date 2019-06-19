licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

proto_library(
    name = "wkt_protos",
    deps = [
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:struct_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

cc_proto_library(
    name = "cc_wkt_protos",
    deps = [":wkt_protos"],
)

envoy_cc_library(
    name = "message_validator_lib",
    srcs = ["message_validator_impl.cc"],
    hdrs = ["message_validator_impl.h"],
    external_deps = ["protobuf"],
    deps = [
        "//include/envoy/protobuf:message_validator_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "protobuf",
    hdrs = ["protobuf.h"],
    external_deps = [
        "protobuf",
    ],
    deps = [":cc_wkt_protos"],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    external_deps = ["protobuf"],
    deps = [
        ":message_validator_lib",
        ":protobuf",
        "//include/envoy/api:api_interface",
        "//include/envoy/protobuf:message_validator_interface",
        "//include/envoy/runtime:runtime_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:utility_lib",
        "//source/common/json:json_loader_lib",
        "@envoy_api//envoy/type:percent_cc",
    ],
)
