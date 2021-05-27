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
        ":filter_state_interface_no_include_prefix",
        "//include/envoy/common:time_interface_no_include_prefix",
        "//include/envoy/http:header_map_interface_no_include_prefix",
        "//include/envoy/http:protocol_interface_no_include_prefix",
        "//include/envoy/http:request_id_extension_interface_no_include_prefix",
        "//include/envoy/network:socket_interface_no_include_prefix",
        "//include/envoy/ssl:connection_interface_no_include_prefix",
        "//include/envoy/upstream:host_description_interface_no_include_prefix",
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
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "uint32_accessor_interface",
    hdrs = ["uint32_accessor.h"],
    deps = [
        ":filter_state_interface_no_include_prefix",
    ],
)
