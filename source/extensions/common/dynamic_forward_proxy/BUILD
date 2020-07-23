load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "dns_cache_interface",
    hdrs = ["dns_cache.h"],
    deps = [
        "//include/envoy/common:backoff_strategy_interface",
        "//include/envoy/common:random_generator_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/singleton:manager_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:resource_manager_interface",
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
        "//include/envoy/network:dns_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/config:utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_lib",
        "@envoy_api//envoy/extensions/common/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "dns_cache_resource_manager",
    srcs = ["dns_cache_resource_manager.cc"],
    hdrs = ["dns_cache_resource_manager.h"],
    deps = [
        ":dns_cache_interface",
        "//include/envoy/common:resource_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:basic_resource_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/upstream:resource_manager_lib",
        "@envoy_api//envoy/extensions/common/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)
