load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "logging_util_test",
    srcs = ["logging_util_test.cc"],
    data = [
        "//test/proto:logging.proto",
        "//test/proto:logging_proto_descriptor",
    ],
    deps = [
        "//source/extensions/filters/http/proto_message_logging/logging_util",
        "//test/proto:logging_proto_cc_proto",
        "//test/test_common:environment_lib",
        "//test/test_common:status_utility_lib",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf",
        "@com_google_protoconverter//:all",
        "@com_google_protofieldextraction//:all_libs",
        "@com_google_protofieldextraction//proto_field_extraction/test_utils:utils",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber:cloud_audit_log_field_checker",
        "@ocp//ocpdiag/core/testing:status_matchers",
    ],
)
