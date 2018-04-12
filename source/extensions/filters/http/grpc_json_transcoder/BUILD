licenses(["notice"])  # Apache 2
# L7 HTTP filter that implements binary gRPC to JSON transcoding
# Public docs: https://github.com/envoyproxy/data-plane-api/blob/master/docs/root/configuration/http_filters/grpc_json_transcoder_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_google_grpc",
)

envoy_package()

envoy_cc_library(
    name = "json_transcoder_filter_lib",
    srcs = ["json_transcoder_filter.cc"],
    hdrs = ["json_transcoder_filter.h"],
    external_deps = [
        "path_matcher",
        "grpc_transcoding",
        "http_api_protos",
    ],
    deps = [
        ":transcoder_input_stream_lib",
        "//include/envoy/http:filter_interface",
        "//source/common/common:base64_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/filter/http/transcoder/v2:transcoder_cc",
    ],
)

envoy_cc_library(
    name = "transcoder_input_stream_lib",
    srcs = ["transcoder_input_stream_impl.cc"],
    hdrs = ["transcoder_input_stream_impl.h"],
    external_deps = ["grpc_transcoding"],
    deps = ["//source/common/buffer:zero_copy_input_stream_lib"],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/extensions/filters/http/grpc_json_transcoder:json_transcoder_filter_lib",
    ],
)
