load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "custom_credentials_provider_chain_test",
    srcs = ["custom_credentials_provider_chain_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "default_credentials_provider_chain_test",
    srcs = ["default_credentials_provider_chain_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
    ],
)
