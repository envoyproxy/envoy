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

envoy_cc_extension(
    name = "registration",
    srcs = ["registration.cc"],
    hdrs = ["registration.h"],
    deps = [
        "extractor_impl",
        "filter",
        "filter_config",
    ],
)
