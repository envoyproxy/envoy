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
        "platform.h",
    ],
    deps = ["@com_google_absl//absl/strings"],
)

envoy_basic_cc_library(
    name = "pure_lib",
    hdrs = [
        "pure.h",
    ],
)

envoy_basic_cc_library(
    name = "exception_lib",
    hdrs = [
        "exception.h",
    ],
    deps = [
        "//source/common/common:assert_lib",
    ],
)

envoy_basic_cc_library(
    name = "optref_lib",
    hdrs = [
        "optref.h",
    ],
    deps = ["@com_google_absl//absl/types:optional"],
)

envoy_cc_library(
    name = "conn_pool_interface",
    hdrs = ["conn_pool.h"],
    deps = [
        "//envoy/event:deferred_deletable",
        "//envoy/upstream:upstream_interface",
    ],
)

envoy_cc_library(
    name = "mutex_tracer",
    hdrs = ["mutex_tracer.h"],
)

envoy_cc_library(
    name = "random_generator_interface",
    hdrs = ["random_generator.h"],
    deps = ["//source/common/common:interval_value"],
)

envoy_cc_library(
    name = "resource_interface",
    hdrs = ["resource.h"],
    deps = [":pure_lib"],
)

envoy_cc_library(
    name = "time_interface",
    hdrs = ["time.h"],
    deps = [":pure_lib"],
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
        "//envoy/config:typed_config_interface",
        "//envoy/server:factory_context_interface",
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
    name = "key_value_store_interface",
    hdrs = ["key_value_store.h"],
    deps = [
        "//envoy/protobuf:message_validator_interface",
        "//envoy/registry",
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
    name = "execution_context",
    hdrs = ["execution_context.h"],
    deps = [
        ":pure_lib",
        ":scope_tracker_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "scope_tracker_interface",
    hdrs = ["scope_tracker.h"],
    deps = [
        ":optref_lib",
        ":pure_lib",
        "//envoy/stream_info:stream_info_interface",
    ],
)

envoy_cc_library(
    name = "hashable_interface",
    hdrs = ["hashable.h"],
)

envoy_cc_library(
    name = "union_string",
    hdrs = [
        "union_string.h",
    ],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/types:variant",
    ],
)
