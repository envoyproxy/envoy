load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "engine_common_test",
    srcs = ["engine_common_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common:engine_common_lib",
    ],
)

envoy_cc_test(
    name = "engine_test",
    srcs = ["engine_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common:envoy_main_interface_lib_no_stamp",
        "//library/common/types:c_types_lib",
        "@envoy//test/common/http:common_lib",
    ],
)

envoy_cc_test(
    name = "main_interface_test",
    srcs = ["main_interface_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common:envoy_main_interface_lib_no_stamp",
        "//library/common/data:utility_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/types:c_types_lib",
        "//test/common/mocks/event:event_mocks",
        "@envoy//test/common/http:common_lib",
    ],
)
