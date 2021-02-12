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
    name = "xff_detection_test",
    extension_name = "envoy.original_ip_detection.xff",
    srcs = ["xff_test.cc"],
    deps = [
        "//source/common/http:utility_lib",
        "//source/extensions/original_ip_detection/xff:xff_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/original_ip_detection/xff/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    extension_name = "envoy.original_ip_detection.xff",
    srcs = ["config_test.cc"],
    deps = [
        "//include/envoy/registry",
        "//source/extensions/original_ip_detection/xff:config",
        "//source/extensions/original_ip_detection/xff:xff_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/original_ip_detection/xff/v3:pkg_cc_proto",
    ],
)
