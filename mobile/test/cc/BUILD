load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")
load("//bazel:envoy_mobile_cc_test.bzl", "envoy_cc_test_with_engine_builder")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "engine_builder_test_shim_lib",
    hdrs = ["engine_builder_test_shim.h"],
    repository = "@envoy",
    visibility = ["//visibility:public"],
    deps = [
        "//library/cc:engine_builder_lib",
    ],
)

envoy_cc_test_with_engine_builder(
    name = "engine_test",
    srcs = ["engine_test.cc"],
    env = {"ENVOY_NO_LOG_SINK": ""},
    repository = "@envoy",
    deps = [
        "//library/common:engine_types_lib",
        "//test/common/integration:engine_with_test_server",
        "//test/common/integration:test_server_lib",
        "@envoy//source/common/common:assert_lib",
        "@envoy_build_config//:test_extensions",
    ],
)
