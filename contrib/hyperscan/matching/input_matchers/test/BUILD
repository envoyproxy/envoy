load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_benchmark_binary",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//contrib/hyperscan/matching/input_matchers/source:config",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_cc_test(
    name = "matcher_test",
    srcs = ["matcher_test.cc"],
    deps = [
        "//contrib/hyperscan/matching/input_matchers/source:matcher_lib",
        "//source/common/thread_local:thread_local_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "hyperscan_speed_test",
    srcs = ["hyperscan_speed_test.cc"],
    external_deps = ["benchmark"],
    deps = [
        "//contrib/hyperscan/matching/input_matchers/source:hyperscan",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "@com_googlesource_code_re2//:re2",
    ],
)

envoy_cc_benchmark_binary(
    name = "matcher_speed_test",
    srcs = ["matcher_speed_test.cc"],
    external_deps = ["benchmark"],
    deps = [
        "//contrib/hyperscan/matching/input_matchers/source:matcher_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:regex_lib",
        "//source/common/common:utility_lib",
        "//source/common/thread_local:thread_local_lib",
    ],
)
