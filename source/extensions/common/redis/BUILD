load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# Redis proxy L4 network filter. Implements consistent hashing and observability for large redis
# clusters.
# Public docs: docs/root/configuration/network_filters/redis_proxy_filter.rst

envoy_package()

envoy_cc_library(
    name = "cluster_refresh_manager_interface",
    hdrs = ["cluster_refresh_manager.h"],
    deps = [
    ],
)

envoy_cc_library(
    name = "cluster_refresh_manager_lib",
    srcs = ["cluster_refresh_manager_impl.cc"],
    hdrs = ["cluster_refresh_manager_impl.h"],
    deps = [
        ":cluster_refresh_manager_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/singleton:manager_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:lock_guard_lib",
        "//source/common/common:thread_annotations",
        "//source/common/common:thread_lib",
    ],
)
