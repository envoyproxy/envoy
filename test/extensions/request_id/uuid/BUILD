load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# This test uses envoy_cc_test versus envoy_extension_cc_test as it is a required extension.
envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/common:random_generator_lib",
        "//source/extensions/request_id/uuid:config",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
    ],
)
