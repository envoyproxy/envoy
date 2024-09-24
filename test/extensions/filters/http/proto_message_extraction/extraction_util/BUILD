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
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/proto_message_extraction/extraction_util",
        "//test/proto:extraction_proto_cc_proto",
        "//test/test_common:environment_lib",
        "//test/test_common:status_utility_lib",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@com_google_protoconverter//:all",
        "@com_google_protofieldextraction//:all_libs",
        "@com_google_protofieldextraction//proto_field_extraction/test_utils:utils",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber:field_mask_path_checker",
        "@ocp//ocpdiag/core/testing:parse_text_proto",
        "@ocp//ocpdiag/core/testing:status_matchers",
    ],
)
