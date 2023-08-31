load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")
load("@envoy//bazel:envoy_select.bzl", "envoy_select_enable_yaml")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "send_headers_test",
    srcs = envoy_select_enable_yaml(
        ["send_headers_test.cc"],
        "@envoy",
    ),
    repository = "@envoy",
    deps = [
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "//test/common/integration:test_engine_builder_lib",
        "@envoy_build_config//:test_extensions",
    ],
)

envoy_cc_test(
    name = "lifetimes_test",
    srcs = envoy_select_enable_yaml(
        ["lifetimes_test.cc"],
        "@envoy",
    ),
    repository = "@envoy",
    deps = [
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "//test/common/integration:test_engine_builder_lib",
        "@envoy_build_config//:test_extensions",
    ],
)
