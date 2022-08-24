load("@rules_proto//proto:defs.bzl", "proto_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

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
        "//envoy/protobuf:message_validator_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:documentation_url_lib",
        "//source/common/common:hash_lib",
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
    name = "utility_lib_header",
    hdrs = ["utility.h"],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/protobuf:message_validator_interface",
        "//source/common/common:statusor_lib",
        "//source/common/common:stl_helpers",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    external_deps = [
        "protobuf",
        "yaml_cpp",
    ],
    deps = [
        ":message_validator_lib",
        ":protobuf",
        ":utility_lib_header",
        "//envoy/runtime:runtime_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:documentation_url_lib",
        "//source/common/common:hash_lib",
        "//source/common/common:utility_lib",
        "//source/common/protobuf:visitor_lib",
        "//source/common/runtime:runtime_features_lib",
        "@com_github_cncf_udpa//udpa/annotations:pkg_cc_proto",
        "@envoy_api//envoy/annotations:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "visitor_lib",
    srcs = ["visitor.cc"],
    hdrs = ["visitor.h"],
    deps = [
        ":message_validator_lib",
        ":protobuf",
        ":utility_lib_header",
        "@com_github_cncf_udpa//udpa/type/v1:pkg_cc_proto",
        "@com_github_cncf_udpa//xds/type/v3:pkg_cc_proto",
    ],
)
