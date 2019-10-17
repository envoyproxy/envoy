licenses(["notice"])  # Apache 2

# Redis proxy L4 network filter. Implements consistent hashing and observability for large redis
# clusters.
# Public docs: docs/root/configuration/network_filters/redis_proxy_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "redirection_mgr_interface",
    hdrs = ["redirection_mgr.h"],
    deps = [
    ],
)

envoy_cc_library(
    name = "redirection_mgr_lib",
    srcs = ["redirection_mgr_impl.cc"],
    hdrs = ["redirection_mgr_impl.h"],
    deps = [
        ":redirection_mgr_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/singleton:manager_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:lock_guard_lib",
        "//source/common/common:thread_annotations",
        "//source/common/common:thread_lib",
    ],
)
