load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter that implements binary gRPC to JSON transcoding
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/grpc_json_transcoder_filter

envoy_extension_package()

envoy_cc_library(
    name = "json_transcoder_filter_lib",
    srcs = ["json_transcoder_filter.cc"],
    hdrs = [
        "json_transcoder_filter.h",
        "stats.h",
    ],
    external_deps = [
        "path_matcher",
        "grpc_transcoding",
        "http_api_protos",
        "api_httpbody_protos",
    ],
    deps = [
        ":http_body_utils_lib",
        ":transcoder_input_stream_lib",
        "//envoy/http:filter_interface",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf",
        "//source/common/runtime:runtime_features_lib",
        "@com_google_googleapis//google/api:http_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/grpc_json_transcoder/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "http_body_utils_lib",
    srcs = ["http_body_utils.cc"],
    hdrs = ["http_body_utils.h"],
    external_deps = ["api_httpbody_protos"],
    deps = [
        "//source/common/grpc:codec_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/extensions/filters/http/grpc_json_transcoder/v3:pkg_cc_proto",
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
    deps = [
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/grpc_json_transcoder:json_transcoder_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/grpc_json_transcoder/v3:pkg_cc_proto",
    ],
)
