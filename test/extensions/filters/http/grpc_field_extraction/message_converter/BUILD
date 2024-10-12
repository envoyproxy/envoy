load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "message_converter_test_lib",
    hdrs = ["message_converter_test_lib.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/grpc:codec_lib",
        "//source/extensions/filters/http/grpc_field_extraction/message_converter:stream_message_lib",
        "//test/proto:apikeys_proto_cc_proto",
        "@com_google_protofieldextraction//:all_libs",
    ],
)

envoy_cc_test(
    name = "message_converter_utility_test",
    srcs = ["message_converter_utility_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":message_converter_test_lib",
        "//source/extensions/filters/http/grpc_field_extraction/message_converter:message_converter_utility_lib",
        "//test/proto:apikeys_proto_cc_proto",
        "//test/test_common:status_utility_lib",
        "@com_google_protofieldextraction//:all_libs",
        "@ocp//ocpdiag/core/testing:status_matchers",
    ],
)

envoy_cc_test(
    name = "message_converter_test",
    srcs = ["message_converter_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":message_converter_test_lib",
        "//source/extensions/filters/http/grpc_field_extraction/message_converter:message_converter_lib",
        "//source/extensions/filters/http/grpc_field_extraction/message_converter:message_converter_utility_lib",
        "//test/proto:apikeys_proto_cc_proto",
        "//test/test_common:status_utility_lib",
        "@com_google_protofieldextraction//:all_libs",
        "@ocp//ocpdiag/core/testing:status_matchers",
    ],
)

envoy_cc_test(
    name = "stream_message_test",
    srcs = ["stream_message_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/grpc_field_extraction/message_converter:stream_message_lib",
    ],
)
