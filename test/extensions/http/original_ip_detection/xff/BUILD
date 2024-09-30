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
    srcs = ["xff_test.cc"],
    extension_names = ["envoy.http.original_ip_detection.xff"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/http/original_ip_detection/xff:xff_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/original_ip_detection/xff/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.http.original_ip_detection.xff"],
    rbe_pool = "2core",
    deps = [
        "//envoy/registry",
        "//source/common/network:address_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/http/original_ip_detection/xff:config",
        "//source/extensions/http/original_ip_detection/xff:xff_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/original_ip_detection/xff/v3:pkg_cc_proto",
    ],
)
