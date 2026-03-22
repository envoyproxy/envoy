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
        "@com_google_absl//absl/container:flat_hash_map",
        "@proto-field-extraction//:all_libs",
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
        "@proto-converter//:all",
        "@proto-field-extraction//:all_libs",
        "@proto-processing//proto_processing_lib/proto_scrubber",
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
        "@proto-converter//:all",
        "@proto-field-extraction//:all_libs",
        "@proto-processing//proto_processing_lib/proto_scrubber",
        "@proto-processing//proto_processing_lib/proto_scrubber:field_mask_path_checker",
    ],
)
