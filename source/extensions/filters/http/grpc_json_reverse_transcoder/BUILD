load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter that implements binary gRPC to JSON transcoding
# Public docs:
envoy_extension_package()

envoy_cc_library(
    name = "utils",
    srcs = ["utils.cc"],
    hdrs = ["utils.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//source/common/http:utility_lib",
        "@com_github_nlohmann_json//:json",
    ],
)

envoy_cc_library(
    name = "filter_config",
    srcs = ["filter_config.cc"],
    hdrs = ["filter_config.h"],
    external_deps = [
        "grpc_transcoding",
    ],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/router:router_interface",
        "//source/common/common:logger_lib",
        "//source/common/grpc:common_lib",
        "@com_google_googleapis//google/api:http_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":filter_config",
        ":utils",
        "//envoy/buffer:buffer_interface",
        "//envoy/grpc:status",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/grpc:status_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/singleton:const_singleton",
        "@com_github_nlohmann_json//:json",
        "@com_google_googleapis//google/api:http_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter",
        ":filter_config",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3:pkg_cc_proto",
    ],
)
