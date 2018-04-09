licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "buffer_lib",
    srcs = ["buffer.cc"],
    hdrs = ["buffer.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:buffer_filter_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "cors_lib",
    srcs = ["cors.cc"],
    hdrs = ["cors.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:cors_filter_lib",
        "//source/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_library(
    name = "fault_lib",
    srcs = ["fault.cc"],
    hdrs = ["fault.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:fault_filter_lib",
    ],
)

envoy_cc_library(
    name = "grpc_http1_bridge_lib",
    srcs = ["grpc_http1_bridge.cc"],
    hdrs = ["grpc_http1_bridge.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/grpc:http1_bridge_filter_lib",
        "//source/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_library(
    name = "grpc_json_transcoder_lib",
    srcs = ["grpc_json_transcoder.cc"],
    hdrs = ["grpc_json_transcoder.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/grpc:json_transcoder_filter_lib",
    ],
)

envoy_cc_library(
    name = "gzip_lib",
    srcs = ["gzip.cc"],
    hdrs = ["gzip.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:gzip_filter_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "grpc_web_lib",
    srcs = ["grpc_web.cc"],
    hdrs = ["grpc_web.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/grpc:grpc_web_filter_lib",
        "//source/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

envoy_cc_library(
    name = "ip_tagging_lib",
    srcs = ["ip_tagging.cc"],
    hdrs = ["ip_tagging.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:ip_tagging_filter_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/filter/http/ip_tagging/v2:ip_tagging_cc",
    ],
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router.cc"],
    hdrs = ["router.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/json:config_schemas_lib",
        "//source/common/router:router_lib",
        "//source/common/router:shadow_writer_lib",
        "@envoy_api//envoy/config/filter/http/router/v2:router_cc",
    ],
)

envoy_cc_library(
    name = "squash_lib",
    srcs = ["squash.cc"],
    hdrs = ["squash.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:squash_filter_lib",
        "//source/common/protobuf:utility_lib",
    ],
)
