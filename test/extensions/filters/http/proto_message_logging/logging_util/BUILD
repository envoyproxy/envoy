load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

cc_test(
    name = "logging_util_test",
    srcs = ["logging_util_test.cc"],
    data = [
        "//test/proto:apikeys_proto_descriptor",
    ],
    deps = [
        "//source/extensions/filters/http/proto_message_logging/logging_util",
        "//test/proto:logging_proto_cc_proto",
        # "//cloud/pulse/logging/public:struct_util",
        # "//net/proto2/bridge/proto:message_set_cc_proto",
        # "//net/proto2/contrib/parse_proto:parse_text_proto",
        # "//third_party/protobuf",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@com_google_protoconverter//:all",
        "@com_google_protofieldextraction//:all_libs",
        # "@com_google_protoprocessinglib//:all_libs",
    ],
)
