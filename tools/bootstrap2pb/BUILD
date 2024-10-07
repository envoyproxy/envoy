load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_binary(
    name = "bootstrap2pb",
    srcs = ["bootstrap2pb.cc"],
    deps = [
        "//source/common/api:api_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:random_generator_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/exe:platform_impl_lib",
        "//test/test_common:test_version_linkstamp",
    ],
)
