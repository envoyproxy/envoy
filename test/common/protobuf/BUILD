licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
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
