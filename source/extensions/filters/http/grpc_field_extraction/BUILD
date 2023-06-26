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

#envoy_cc_library(
#    name = "json_transcoder_filter_lib",
#    srcs = ["json_transcoder_filter.cc"],
#    hdrs = ["json_transcoder_filter.h"],
#    external_deps = [
#        "path_matcher",
#        "grpc_transcoding",
#        "http_api_protos",
#        "api_httpbody_protos",
#    ],
#    deps = [
#        ":http_body_utils_lib",
#        ":transcoder_input_stream_lib",
#        "//envoy/http:filter_interface",
#        "//source/common/grpc:codec_lib",
#        "//source/common/grpc:common_lib",
#        "//source/common/http:headers_lib",
#        "//source/common/protobuf",
#        "//source/common/runtime:runtime_features_lib",
#        "@com_google_googleapis//google/api:http_cc_proto",
#        "@envoy_api//envoy/extensions/filters/http/grpc_json_transcoder/v3:pkg_cc_proto",
#    ],
#)
#
#envoy_cc_library(
#    name = "http_body_utils_lib",
#    srcs = ["http_body_utils.cc"],
#    hdrs = ["http_body_utils.h"],
#    external_deps = [
#        "api_httpbody_protos",
#    ],
#    deps = [
#        "//source/common/grpc:codec_lib",
#        "//source/common/protobuf",
#    ],
#)
#
#envoy_cc_library(
#    name = "transcoder_input_stream_lib",
#    srcs = ["transcoder_input_stream_impl.cc"],
#    hdrs = ["transcoder_input_stream_impl.h"],
#    external_deps = ["grpc_transcoding"],
#    deps = ["//source/common/buffer:zero_copy_input_stream_lib"],
#)

envoy_cc_library(
    name = "extractor",
    hdrs = ["extractor.h"],
    external_deps = ["grpc_transcoding"],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_protofieldextraction//:all_libs",
        "@envoy_api//envoy/extensions/filters/http/grpc_field_extraction/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "extractor_impl",
    srcs = ["extractor_impl.cc"],
    hdrs = ["extractor_impl.h"],
    external_deps = [
        "grpc_transcoding",
        "protobuf",
    ],
    deps = [
        "extractor",
        #        "//source/common/grpc:common_lib",
        "@com_google_absl//absl/status",
        "//source/common/common:minimal_logger_lib",
        "@com_google_protofieldextraction//:all_libs",
        "@envoy_api//envoy/extensions/filters/http/grpc_field_extraction/v3:pkg_cc_proto",
        #        "@ocp//ocpdiag/core/compat:status_macros",
    ],
)

#cc_library(
#    name = "extractor_impl",
#    srcs = ["extractor_impl.cc"],
#    hdrs = ["extractor_impl.h"],
#    deps = [
#        ":extractor",
#        "//experimental/users/taoxuy/grpc_field_extraction/filter:grpc_field_extraction_proto_cc_proto",
#        "//google/api:policy_cc_proto",
#        "//net/proto2/proto:descriptor_cc_proto",
#        "//third_party/envoy/src/source/common/grpc:common_lib",
#        "//third_party/grpc_httpjson_transcoding/src:type_helper",
#        "//third_party/proto_field_extraction/src/field_value_extractor:field_value_extractor_factory",
#        "//third_party/proto_field_extraction/src/field_value_extractor:field_value_extractor_interface",
#        "//util/task:status",
#        "@com_google_protofieldextraction//:all_libs",
#    ],
#)

envoy_cc_extension(
    name = "filter_config",
    srcs = ["filter_config.cc"],
    hdrs = ["filter_config.h"],
    external_deps = ["protobuf"],
    deps = [
        ":extractor",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/grpc_json_transcoder:json_transcoder_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/grpc_field_extraction/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    external_deps = [
        "grpc_transcoding",
        "protobuf",
    ],
    deps = [
        "extractor_impl",
        "filter_config",
        "//source/common/http:codes_lib",
        "//source/common/protobuf:message_converter_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)
