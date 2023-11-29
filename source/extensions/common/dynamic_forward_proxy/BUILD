load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "dns_cache_interface",
    hdrs = ["dns_cache.h"],
    deps = [
        "//envoy/common:backoff_strategy_interface",
        "//envoy/common:random_generator_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:resource_manager_interface",
        "@envoy_api//envoy/extensions/common/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "dns_cache_manager_impl",
    srcs = ["dns_cache_manager_impl.cc"],
    hdrs = ["dns_cache_manager_impl.h"],
    deps = [
        ":dns_cache_impl",
        "//source/common/protobuf",
        "//source/server:factory_context_base_impl_lib",
        "@envoy_api//envoy/extensions/common/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "dns_cache_impl",
    srcs = ["dns_cache_impl.cc"],
    hdrs = ["dns_cache_impl.h"],
    deps = [
        ":dns_cache_interface",
        ":dns_cache_resource_manager",
        "//envoy/network:dns_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/common:dns_utils_lib",
        "//source/common/common:key_value_store_lib",
        "//source/common/config:utility_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:utility_lib",
        "//source/common/network/dns_resolver:dns_factory_util_lib",
        "//source/server:generic_factory_context_lib",
        "@envoy_api//envoy/extensions/common/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "dns_cache_resource_manager",
    srcs = ["dns_cache_resource_manager.cc"],
    hdrs = ["dns_cache_resource_manager.h"],
    deps = [
        ":dns_cache_interface",
        "//envoy/common:resource_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:basic_resource_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/upstream:resource_manager_lib",
        "@envoy_api//envoy/extensions/common/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "cluster_store",
    srcs = ["cluster_store.cc"],
    hdrs = ["cluster_store.h"],
    deps = [
        "//envoy/server:factory_context_interface",
        "//envoy/upstream:upstream_interface",
        "//source/server:factory_context_base_impl_lib",
        "@com_google_absl//absl/container:flat_hash_set",
    ],
)
