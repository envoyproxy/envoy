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
    name = "extracted_external_address_detection_test",
    srcs = ["extracted_external_address_test.cc"],
    extension_names = ["envoy.http.original_ip_detection.extracted_external_address"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/network:utility_lib",
        "//source/extensions/http/original_ip_detection/extracted_external_address:extracted_external_address_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/original_ip_detection/extracted_external_address/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.http.original_ip_detection.extracted_external_address"],
    rbe_pool = "6gig",
    deps = [
        "//envoy/registry",
        "//source/extensions/http/original_ip_detection/extracted_external_address:config",
        "//source/extensions/http/original_ip_detection/extracted_external_address:extracted_external_address_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/original_ip_detection/extracted_external_address/v3:pkg_cc_proto",
    ],
)
