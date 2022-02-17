load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "local_info_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//envoy/local_info:local_info_interface",
        "//source/common/network:address_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/config:config_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
