load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "message_validator_impl_test",
    srcs = ["message_validator_impl_test.cc"],
    deps = [
        "//source/common/protobuf:message_validator_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        "//source/common/config:api_version_lib",
        "//source/common/protobuf:utility_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/init:init_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/proto:deprecated_proto_cc_proto",
        "//test/proto:sensitive_proto_cc_proto",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/api/v2/core:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "type_util_test",
    srcs = ["type_util_test.cc"],
    deps = [
        "//source/common/protobuf:type_util_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "value_util_fuzz_test",
    srcs = ["value_util_fuzz_test.cc"],
    corpus = "value_util_corpus",
    # Fuzzer is stable, no bugs, simple test target; avoid emitting CO2.
    tags = ["no_fuzz"],
    deps = ["//source/common/protobuf:utility_lib"],
)
