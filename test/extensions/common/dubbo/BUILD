load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "message_test",
    srcs = ["message_test.cc"],
    deps = [
        "//source/extensions/common/dubbo:codec_lib",
        "//source/extensions/common/dubbo:message_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "metadata_test",
    srcs = ["metadata_test.cc"],
    deps = [
        "//source/extensions/common/dubbo:metadata_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "hessian2_utils_test",
    srcs = ["hessian2_utils_test.cc"],
    deps = [
        "//source/extensions/common/dubbo:hessian2_utils_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "hessian2_serializer_impl_test",
    srcs = ["hessian2_serializer_impl_test.cc"],
    deps = [
        "//source/extensions/common/dubbo:hessian2_serializer_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_mock(
    name = "mocks_lib",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/extensions/common/dubbo:serializer_interface",
    ],
)

envoy_cc_test(
    name = "codec_test",
    srcs = ["codec_test.cc"],
    deps = [
        ":mocks_lib",
        "//source/extensions/common/dubbo:codec_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)
