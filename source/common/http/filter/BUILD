licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "cors_filter_lib",
    srcs = ["cors_filter.cc"],
    hdrs = ["cors_filter.h"],
    deps = [
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
    ],
)

envoy_cc_library(
    name = "fault_filter_lib",
    srcs = ["fault_filter.cc"],
    hdrs = ["fault_filter.h"],
    deps = [
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:config_lib",
        "@envoy_api//envoy/config/filter/http/fault/v2:fault_cc",
    ],
)

envoy_cc_library(
    name = "gzip_filter_lib",
    srcs = ["gzip_filter.cc"],
    hdrs = ["gzip_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/json:json_object_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/compressor:compressor_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_validator_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/filter/http/gzip/v2:gzip_cc",
    ],
)

envoy_cc_library(
    name = "ip_tagging_filter_lib",
    srcs = ["ip_tagging_filter.cc"],
    hdrs = ["ip_tagging_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/runtime:runtime_interface",
        "//source/common/common:assert_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:lc_trie_lib",
        "@envoy_api//envoy/config/filter/http/ip_tagging/v2:ip_tagging_cc",
    ],
)
