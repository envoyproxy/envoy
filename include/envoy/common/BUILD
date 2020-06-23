load(
    "//bazel:envoy_build_system.bzl",
    "envoy_basic_cc_library",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_basic_cc_library(
    name = "base_includes",
    hdrs = [
        "exception.h",
        "platform.h",
        "pure.h",
    ],
    include_prefix = "envoy/common",
)

envoy_cc_library(
    name = "conn_pool_interface",
    hdrs = ["conn_pool.h"],
    deps = [
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/upstream:upstream_interface",
    ],
)

envoy_cc_library(
    name = "mutex_tracer",
    hdrs = ["mutex_tracer.h"],
)

envoy_cc_library(
    name = "resource_interface",
    hdrs = ["resource.h"],
)

envoy_cc_library(
    name = "time_interface",
    hdrs = ["time.h"],
)

envoy_cc_library(
    name = "matchers_interface",
    hdrs = ["matchers.h"],
)

envoy_cc_library(
    name = "regex_interface",
    hdrs = ["regex.h"],
    deps = [
        ":matchers_interface",
    ],
)

envoy_cc_library(
    name = "token_bucket_interface",
    hdrs = ["token_bucket.h"],
    deps = [
        ":time_interface",
    ],
)

envoy_cc_library(
    name = "interval_set_interface",
    hdrs = ["interval_set.h"],
)

envoy_cc_library(
    name = "callback",
    hdrs = ["callback.h"],
)

envoy_cc_library(
    name = "backoff_strategy_interface",
    hdrs = ["backoff_strategy.h"],
)

envoy_cc_library(
    name = "scope_tracker_interface",
    hdrs = ["scope_tracker.h"],
)
