load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load(
    "@envoy//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "platform_store_test",
    srcs = ["platform_store_test.cc"],
    extension_names = ["envoy.key_value.platform"],
    repository = "@envoy",
    deps = [
        "//library/common/extensions/key_value/platform:config",
        "@envoy//source/common/common:key_value_store_lib",
        "@envoy//test/mocks/event:event_mocks",
        "@envoy//test/test_common:file_system_for_test_lib",
    ],
)
