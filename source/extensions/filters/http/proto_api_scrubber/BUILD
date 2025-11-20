load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "filter_config",
    srcs = ["filter_config.cc"],
    hdrs = ["filter_config.h"],
    external_deps = ["grpc_transcoding"],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/matcher:matcher_lib",
        "//source/common/protobuf:utility_lib",
        "@com_github_cncf_xds//xds/type/matcher/v3:pkg_cc_proto",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/proto_api_scrubber/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "filter_config",
        "//source/common/grpc:common_lib",
        "//source/common/http:codes_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//source/extensions/filters/http/grpc_field_extraction/message_converter:message_converter_lib",
        "//source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber",
        "@envoy_api//envoy/extensions/filters/http/proto_api_scrubber/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "filter",
        "filter_config",
        "@envoy_api//envoy/extensions/filters/http/proto_api_scrubber/v3:pkg_cc_proto",
    ],
)
