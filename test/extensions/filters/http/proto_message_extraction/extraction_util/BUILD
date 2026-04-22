load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "extraction_util_test",
    srcs = ["extraction_util_test.cc"],
    data = [
        "//test/proto:extraction.proto",
        "//test/proto:extraction_proto_descriptor",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/proto_message_extraction/extraction_util",
        "//test/proto:extraction_proto_cc_proto",
        "//test/test_common:environment_lib",
        "//test/test_common:status_utility_lib",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@ocp-diag-core//ocpdiag/core/testing:parse_text_proto",
        "@ocp-diag-core//ocpdiag/core/testing:status_matchers",
        "@proto-converter//:all",
        "@proto-field-extraction//:all_libs",
        "@proto-field-extraction//proto_field_extraction/test_utils:utils",
        "@proto-processing//proto_processing_lib/proto_scrubber:field_mask_path_checker",
    ],
)
