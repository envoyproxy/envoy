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
        ":stream_id_provider_interface",
        "//envoy/common:time_interface",
        "//envoy/http:header_map_interface",
        "//envoy/http:protocol_interface",
        "//envoy/network:socket_interface",
        "//envoy/ssl:connection_interface",
        "//envoy/tracing:trace_reason_interface",
        "//envoy/upstream:host_description_interface",
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
        "//envoy/config:typed_config_interface",
        "//source/common/common:fmt_lib",
        "//source/common/common:utility_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "uint32_accessor_interface",
    hdrs = ["uint32_accessor.h"],
    deps = [
        ":filter_state_interface",
    ],
)

envoy_cc_library(
    name = "uint64_accessor_interface",
    hdrs = ["uint64_accessor.h"],
    deps = [
        ":filter_state_interface",
    ],
)

envoy_cc_library(
    name = "stream_id_provider_interface",
    hdrs = ["stream_id_provider.h"],
)

envoy_cc_library(
    name = "bool_accessor_interface",
    hdrs = ["bool_accessor.h"],
    deps = [
        ":filter_state_interface",
    ],
)
