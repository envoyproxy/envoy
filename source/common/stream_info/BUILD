load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "stream_info_lib",
    hdrs = ["stream_info_impl.h"],
    deps = [
        ":filter_state_lib",
        ":stream_id_provider_lib",
        "//envoy/http:request_id_extension_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:dump_state_utils",
        "//source/common/common:empty_string",
        "//source/common/network:socket_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_state_lib",
    srcs = ["filter_state_impl.cc"],
    hdrs = ["filter_state_impl.h"],
    deps = [
        "//envoy/stream_info:filter_state_interface",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/http:codes_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/http:default_server_string_lib",
        "//source/common/runtime:runtime_features_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "uint32_accessor_lib",
    hdrs = ["uint32_accessor_impl.h"],
    deps = [
        "//envoy/stream_info:uint32_accessor_interface",
    ],
)

envoy_cc_library(
    name = "uint64_accessor_lib",
    hdrs = ["uint64_accessor_impl.h"],
    deps = [
        "//envoy/stream_info:uint64_accessor_interface",
    ],
)

envoy_cc_library(
    name = "upstream_address_lib",
    hdrs = ["upstream_address.h"],
    deps = [
        "//envoy/stream_info:filter_state_interface",
    ],
)

envoy_cc_library(
    name = "stream_id_provider_lib",
    srcs = ["stream_id_provider_impl.cc"],
    hdrs = ["stream_id_provider_impl.h"],
    deps = [
        "//envoy/stream_info:stream_id_provider_interface",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "bool_accessor_lib",
    hdrs = ["bool_accessor_impl.h"],
    deps = [
        "//envoy/stream_info:bool_accessor_interface",
    ],
)
