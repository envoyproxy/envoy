load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load(
    "@envoy//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_extension_cc_test(
    name = "platform_bridge_cert_validator_test",
    srcs = ["platform_bridge_cert_validator_test.cc"],
    data = [
        "@envoy//test/common/tls/test_data:certs",
    ],
    extension_names = ["envoy_mobile.cert_validator.platform_bridge_cert_validator"],
    repository = "@envoy",
    deps = [
        "//library/common/extensions/cert_validator/platform_bridge:config",
        "//library/common/extensions/cert_validator/platform_bridge:platform_bridge_cc_proto",
        "//test/common/mocks/common:common_mocks",
        "@envoy//source/common/tls/cert_validator:cert_validator_lib",
        "@envoy//test/common/tls:ssl_test_utils",
        "@envoy//test/common/tls/cert_validator:test_common",
        "@envoy//test/common/tls/test_data:cert_infos",
        "@envoy//test/mocks/event:event_mocks",
        "@envoy//test/mocks/ssl:ssl_mocks",
        "@envoy//test/mocks/thread:thread_mocks",
        "@envoy//test/test_common:environment_lib",
        "@envoy//test/test_common:file_system_for_test_lib",
        "@envoy//test/test_common:test_runtime_lib",
    ],
)
