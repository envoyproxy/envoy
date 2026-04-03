load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "string_returning_action_test",
    srcs = ["string_returning_action_test.cc"],
    rbe_pool = "6gig",
    deps = [
        # formatter_extension_lib is required for DYNAMIC_METADATA to function in test.
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/matcher/actions:string_returning_action_lib",
        "//test/test_common:utility_lib",
        "//test/mocks/server:instance_mocks",
    ],
)
