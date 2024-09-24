load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "dns_cache_impl_test",
    srcs = ["dns_cache_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        "//source/common/config:utility_lib",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_impl",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_manager_impl",
        "//source/extensions/network/dns_resolver/cares:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/dynamic_forward_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/key_value/file_based/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "dns_cache_resource_manager_test",
    srcs = ["dns_cache_resource_manager_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":mocks",
        "//source/common/config:utility_lib",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_impl",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_resource_manager",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/common/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/extensions/common/dynamic_forward_proxy:cluster_store",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_impl",
        "//test/mocks/upstream:basic_resource_limit_mocks",
        "@envoy_api//envoy/extensions/common/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)
