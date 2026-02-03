load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "extractor",
    hdrs = ["extractor.h"],
    external_deps = ["grpc_transcoding"],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_protofieldextraction//:all_libs",
        "@envoy_api//envoy/extensions/filters/http/proto_message_extraction/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "extractor_impl",
    srcs = ["extractor_impl.cc"],
    hdrs = ["extractor_impl.h"],
    deps = [
        "extractor",
        "//source/common/common:minimal_logger_lib",
        "//source/common/protobuf",
        "//source/extensions/filters/http/proto_message_extraction/extraction_util:proto_extractor",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_protoconverter//:all",
        "@com_google_protofieldextraction//:all_libs",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber:field_mask_path_checker",
        "@envoy_api//envoy/extensions/filters/http/proto_message_extraction/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "filter_config",
    srcs = ["filter_config.cc"],
    hdrs = ["filter_config.h"],
    deps = [
        ":extractor_impl",
        "//source/common/grpc:common_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_protoconverter//:all",
        "@com_google_protofieldextraction//:all_libs",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber:field_mask_path_checker",
        "@envoy_api//envoy/extensions/filters/http/proto_message_extraction/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "extractor_impl",
        "filter_config",
        "//source/common/http:codes_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//source/extensions/filters/http/grpc_field_extraction/message_converter:message_converter_lib",
        "@envoy_api//envoy/extensions/filters/http/proto_message_extraction/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "extractor_impl",
        "filter",
        "filter_config",
        "@envoy_api//envoy/extensions/filters/http/proto_message_extraction/v3:pkg_cc_proto",
    ],
)
