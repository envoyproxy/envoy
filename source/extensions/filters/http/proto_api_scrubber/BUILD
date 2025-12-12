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
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/matcher:matcher_lib",
        "@com_github_cncf_xds//xds/type/matcher/v3:pkg_cc_proto",
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
