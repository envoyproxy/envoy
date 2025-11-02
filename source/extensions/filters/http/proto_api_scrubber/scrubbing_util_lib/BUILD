load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "scrubbing_util_lib",
    srcs = [
        "field_checker.cc",
    ],
    hdrs = [
        "field_checker.h",
    ],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/protobuf",
        "//source/extensions/filters/http/proto_api_scrubber:filter_config",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber",
    ],
)
