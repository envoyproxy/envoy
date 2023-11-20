load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_mobile_package",
)

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_mock(
    name = "common_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    repository = "@envoy",
    deps = [
        "//library/common/common:system_helper_lib",
    ],
)
