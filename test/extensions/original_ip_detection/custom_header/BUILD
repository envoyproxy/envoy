load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "custom_header_detection_test",
    extension_name = "envoy.original_ip_detection.custom_header",
    srcs = ["custom_header_test.cc"],
    deps = [
        "//source/common/network:utility_lib",
        "//source/extensions/original_ip_detection/custom_header:custom_header_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/original_ip_detection/custom_header/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    extension_name = "envoy.original_ip_detection.custom_header",
    srcs = ["config_test.cc"],
    deps = [
        "//include/envoy/registry",
        "//source/extensions/original_ip_detection/custom_header:config",
        "//source/extensions/original_ip_detection/custom_header:custom_header_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/original_ip_detection/custom_header/v3:pkg_cc_proto",
    ],
)
