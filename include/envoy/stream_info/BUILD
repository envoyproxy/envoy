load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "stream_info_interface",
    hdrs = ["stream_info.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":filter_state_interface",
        "//include/envoy/common:time_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/http:protocol_interface",
        "//include/envoy/http:request_id_extension_interface",
        "//include/envoy/ssl:connection_interface",
        "//include/envoy/upstream:host_description_interface",
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
        "//source/common/singleton:const_singleton",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_state_interface",
    hdrs = ["filter_state.h"],
    external_deps = ["abseil_optional"],
    deps = ["//source/common/protobuf"],
)

envoy_cc_library(
    name = "uint32_accessor_interface",
    hdrs = ["uint32_accessor.h"],
    deps = [
        ":filter_state_interface",
    ],
)
