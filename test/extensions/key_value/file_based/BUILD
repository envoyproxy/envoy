load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "key_value_store_test",
    srcs = ["key_value_store_test.cc"],
    deps = [
        "//source/common/common:key_value_store_lib",
        "//source/extensions/key_value/file_based:config_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:file_system_for_test_lib",
    ],
)
