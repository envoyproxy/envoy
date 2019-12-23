licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_platform_dep",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_cc_test_library(
    name = "mocks_lib",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/extensions/common/redis:cluster_refresh_manager_interface",
    ],
)

envoy_extension_cc_test(
    name = "cluster_refresh_manager_test",
    srcs = ["cluster_refresh_manager_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    deps = [
        "//source/common/common:lock_guard_lib",
        "//source/common/common:thread_lib",
        "//source/extensions/common/redis:cluster_refresh_manager_lib",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/extensions/filters/network/redis_proxy:redis_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ] + envoy_cc_platform_dep("//source/exe:platform_impl_lib"),
)
