load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_mobile_package")
load("@envoy//bazel:envoy_select.bzl", "envoy_select_enable_yaml")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test(
    name = "engine_test",
    srcs = envoy_select_enable_yaml(
        ["engine_test.cc"],
        "@envoy",
    ),
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "@envoy_build_config//:test_extensions",
    ],
)
