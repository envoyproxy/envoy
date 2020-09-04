load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "envoy_mobile_main_common_test",
    srcs = ["envoy_mobile_main_common_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common:envoy_mobile_main_common_lib",
    ],
)

envoy_cc_test(
    name = "engine_test",
    srcs = ["engine_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common:envoy_main_interface_lib_no_stamp",
        "//library/common/types:c_types_lib",
    ],
)
