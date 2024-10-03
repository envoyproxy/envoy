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
    rbe_pool = "2core",
    deps = [
        "//contrib/hyperscan/matching/input_matchers/source:config",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_cc_test(
    name = "matcher_test",
    srcs = ["matcher_test.cc"],
    deps = [
        "//source/common/thread_local:thread_local_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:utility_lib",
    ] + select({
        "//bazel:linux_x86_64": [
            "//contrib/hyperscan/matching/input_matchers/source:hyperscan_matcher_lib",
        ],
        "//bazel:linux_aarch64": [
            "//contrib/hyperscan/matching/input_matchers/source:vectorscan_matcher_lib",
        ],
    }),
)

envoy_cc_benchmark_binary(
    name = "hyperscan_speed_test",
    srcs = ["hyperscan_speed_test.cc"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "@com_github_google_benchmark//:benchmark",
        "@com_googlesource_code_re2//:re2",
    ] + select({
        "//bazel:linux_x86_64": [
            "//contrib/hyperscan/matching/input_matchers/source:hyperscan",
        ],
        "//bazel:linux_aarch64": [
            "//contrib/hyperscan/matching/input_matchers/source:vectorscan",
        ],
    }),
)

envoy_cc_benchmark_binary(
    name = "matcher_speed_test",
    srcs = ["matcher_speed_test.cc"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:regex_lib",
        "//source/common/common:utility_lib",
        "//source/common/thread_local:thread_local_lib",
        "//test/mocks/event:event_mocks",
        "@com_github_google_benchmark//:benchmark",
    ] + select({
        "//bazel:linux_x86_64": [
            "//contrib/hyperscan/matching/input_matchers/source:hyperscan_matcher_lib",
        ],
        "//bazel:linux_aarch64": [
            "//contrib/hyperscan/matching/input_matchers/source:vectorscan_matcher_lib",
        ],
    }),
)
