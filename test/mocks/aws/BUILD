licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "aws_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/common/aws:credentials_provider_lib",
        "//source/common/aws:metadata_fetcher_lib",
        "//source/common/aws:region_provider_lib",
        "//source/common/aws:signer_lib",
    ],
)
