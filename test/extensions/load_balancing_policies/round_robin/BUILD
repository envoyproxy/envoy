load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.load_balancing_policies.round_robin"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_large",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.load_balancing_policies.round_robin"],
    # linux_x64_xlarge: the coverage link peaks at 15009MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "cto": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_xlarge",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "asan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//source/common/protobuf",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "round_robin_load_balancer_fuzz_proto",
    srcs = ["round_robin_load_balancer_fuzz.proto"],
    deps = [
        "//test/extensions/load_balancing_policies/common:zone_aware_load_balancer_fuzz_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "round_robin_load_balancer_fuzz_test",
    srcs = ["round_robin_load_balancer_fuzz_test.cc"],
    corpus = "round_robin_load_balancer_corpus",
    # linux_x64_xlarge: the fuzz coverage link peaks at 11543MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "tsan": {"link": "linux_x64_small"},
        "fuzz_coverage": {"link": "linux_x64_xlarge"},
    },
    deps = [
        ":round_robin_load_balancer_fuzz_proto_cc_proto",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//test/common/upstream:utility_lib",
        "//test/extensions/load_balancing_policies/common:zone_aware_load_balancer_fuzz_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "round_robin_lb_benchmark",
    srcs = ["round_robin_lb_benchmark.cc"],
    rbe_pool = {"tsan": {"link": "linux_x64_small"}},
    deps = [
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//test/extensions/load_balancing_policies/common:benchmark_base_tester_lib",
    ],
)

envoy_benchmark_test(
    name = "round_robin_lb_benchmark_test",
    timeout = "long",
    benchmark_binary = "round_robin_lb_benchmark",
)

envoy_extension_cc_test(
    name = "round_robin_lb_test",
    srcs = ["round_robin_lb_test.cc"],
    extension_names = ["envoy.load_balancing_policies.round_robin"],
    rbe_pool = {
        "gcc": {"build": "linux_x64_small"},
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/extensions/load_balancing_policies/round_robin:round_robin_lb_lib",
        "//test/extensions/load_balancing_policies/common:load_balancer_base_test_lib",
    ],
)
