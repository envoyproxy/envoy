load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "field_checker_test",
    srcs = ["field_checker_test.cc"],
    deps = [
        "//source/common/protobuf",
        "//source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber",
    ],
)
