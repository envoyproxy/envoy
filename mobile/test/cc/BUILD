load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test(
    name = "engine_test",
    srcs = ["engine_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common:engine_types_lib",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy//source/common/common:assert_lib",
        "@envoy_build_config//:test_extensions",
    ],
)
