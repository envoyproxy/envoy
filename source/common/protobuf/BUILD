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
        ":protobuf",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:utility_lib",
        "//source/common/filesystem:filesystem_lib",
        "//source/common/json:json_loader_lib",
        "@envoy_api//envoy/type:percent_cc",
    ],
)
