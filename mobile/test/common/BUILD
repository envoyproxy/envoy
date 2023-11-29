load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_mobile_package")
load("@envoy//bazel:envoy_select.bzl", "envoy_select_enable_yaml")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test(
    name = "engine_common_test",
    srcs = ["engine_common_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common:engine_common_lib",
        "@envoy_build_config//:extension_registry",
    ],
)

envoy_cc_test(
    name = "engine_test",
    srcs = envoy_select_enable_yaml(
        ["engine_test.cc"],
        "@envoy",
    ),
    repository = "@envoy",
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common:envoy_main_interface_lib_no_stamp",
        "//library/common/types:c_types_lib",
        "@envoy//test/common/http:common_lib",
    ],
)

envoy_cc_test(
    name = "main_interface_test",
    srcs = envoy_select_enable_yaml(
        ["main_interface_test.cc"],
        "@envoy",
    ),
    repository = "@envoy",
    deps = [
        "//library/common:envoy_main_interface_lib_no_stamp",
        "//library/common/data:utility_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/types:c_types_lib",
        "//test/common/mocks/common:common_mocks",
        "//test/common/mocks/event:event_mocks",
        "@envoy//test/common/http:common_lib",
        "@envoy_build_config//:test_extensions",
    ],
)
