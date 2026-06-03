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
    deps = ["@abseil-cpp//absl/strings"],
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
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

envoy_basic_cc_library(
    name = "optref_lib",
    hdrs = [
        "optref.h",
    ],
    deps = ["@abseil-cpp//absl/types:optional"],
)

envoy_cc_library(
    name = "conn_pool_interface",
    hdrs = ["conn_pool.h"],
    deps = [
        ":pure_lib",
        "//envoy/upstream:host_description_interface",
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
    deps = [
        ":optref_lib",
        "//envoy/stream_info:stream_info_interface",
    ],
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
        ":pure_lib",
        "//envoy/config:typed_config_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/filesystem:filesystem_interface",
        "//envoy/protobuf:message_validator_interface",
        "//source/common/protobuf",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "interval_set_interface",
    hdrs = ["interval_set.h"],
    deps = [
        ":pure_lib",
    ],
)

envoy_cc_library(
    name = "callback",
    hdrs = ["callback.h"],
)

envoy_cc_library(
    name = "backoff_strategy_interface",
    hdrs = ["backoff_strategy.h"],
    deps = [
        ":pure_lib",
    ],
)

envoy_cc_library(
    name = "execution_context",
    hdrs = ["execution_context.h"],
    deps = [
        ":optref_lib",
        ":pure_lib",
        ":scope_tracker_interface",
        "//envoy/stream_info:filter_state_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/common:macros",
        "//source/common/common:non_copyable",
        "@abseil-cpp//absl/strings",
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
    deps = [
        ":pure_lib",
        "@abseil-cpp//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "union_string",
    hdrs = [
        "union_string.h",
    ],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "@abseil-cpp//absl/container:inlined_vector",
        "@abseil-cpp//absl/types:variant",
    ],
)
