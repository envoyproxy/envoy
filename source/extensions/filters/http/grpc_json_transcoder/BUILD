load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter that implements binary gRPC to JSON transcoding
# Public docs: docs/root/configuration/http_filters/grpc_json_transcoder_filter.rst

envoy_package()

envoy_cc_library(
    name = "json_transcoder_filter_lib",
    srcs = ["json_transcoder_filter.cc"],
    hdrs = ["json_transcoder_filter.h"],
    external_deps = [
        "path_matcher",
        "grpc_transcoding",
        "http_api_protos",
        "api_httpbody_protos",
    ],
    deps = [
        ":http_body_utils_lib",
        ":transcoder_input_stream_lib",
        "//include/envoy/http:filter_interface",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/extensions/filters/http/grpc_json_transcoder/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "http_body_utils_lib",
    srcs = ["http_body_utils.cc"],
    hdrs = ["http_body_utils.h"],
    external_deps = [
        "api_httpbody_protos",
    ],
    deps = [
        "//source/common/grpc:codec_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "transcoder_input_stream_lib",
    srcs = ["transcoder_input_stream_impl.cc"],
    hdrs = ["transcoder_input_stream_impl.h"],
    external_deps = ["grpc_transcoding"],
    deps = ["//source/common/buffer:zero_copy_input_stream_lib"],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "unknown",
    deps = [
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/grpc_json_transcoder:json_transcoder_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/grpc_json_transcoder/v3:pkg_cc_proto",
    ],
)
