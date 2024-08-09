load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "proto_scrubber_interface",
    hdrs = [
        "proto_scrubber_interface.h",
    ],
    deps = [
        "//source/common/protobuf",
        "//source/common/protobuf:cc_wkt_protos",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_protofieldextraction//:all_libs",
    ],
)

envoy_cc_library(
    name = "logging_util",
    srcs = [
        "logging_util.cc",
    ],
    hdrs = [
        "logging_util.h",
    ],
    deps = [
        ":proto_scrubber_interface",
        "//source/common/common:regex_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:cc_wkt_protos",
        "@com_google_protoconverter//:all",
        "@com_google_protofieldextraction//:all_libs",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber",
    ],
)
