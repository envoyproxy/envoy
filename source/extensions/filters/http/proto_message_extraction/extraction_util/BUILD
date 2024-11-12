load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "proto_extractor_interface",
    hdrs = [
        "proto_extractor_interface.h",
    ],
    deps = [
        "//source/common/protobuf",
        "//source/common/protobuf:cc_wkt_protos",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_protofieldextraction//:all_libs",
    ],
)

envoy_cc_library(
    name = "extraction_util",
    srcs = [
        "extraction_util.cc",
    ],
    hdrs = [
        "extraction_util.h",
    ],
    external_deps = ["grpc_transcoding"],
    deps = [
        ":proto_extractor_interface",
        "//source/common/common:regex_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:cc_wkt_protos",
        "@com_google_protoconverter//:all",
        "@com_google_protofieldextraction//:all_libs",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber",
    ],
)

envoy_cc_library(
    name = "proto_extractor",
    srcs = [
        "proto_extractor.cc",
    ],
    hdrs = [
        "proto_extractor.h",
    ],
    deps = [
        ":extraction_util",
        ":proto_extractor_interface",
        "//source/common/common:regex_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:cc_wkt_protos",
        "@com_google_protoconverter//:all",
        "@com_google_protofieldextraction//:all_libs",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber:field_mask_path_checker",
    ],
)
