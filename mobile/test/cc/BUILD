load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test(
    name = "engine_test",
    srcs = ["engine_test.cc"],
    env = {"ENVOY_NO_LOG_SINK": ""},
    repository = "@envoy",
    deps = [
        "@envoy//source/common/common:assert_lib",
        "@envoy_build_config//:test_extensions",
        "@envoy_mobile//library/cc:engine_builder_lib",
        "@envoy_mobile//library/common:engine_types_lib",
        "@envoy_mobile//test/common/integration:engine_with_test_server",
        "@envoy_mobile//test/common/integration:test_server_lib",
    ],
)
