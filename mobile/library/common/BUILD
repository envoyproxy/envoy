load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package", "envoy_select_signal_trace")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "internal_engine_lib",
    repository = "@envoy",
    deps = [
        ":engine_common_lib_stamped",
        ":internal_engine_lib_no_stamp",
    ],
)

envoy_cc_library(
    name = "internal_engine_lib_no_stamp",
    srcs = [
        "internal_engine.cc",
    ],
    hdrs = [
        "internal_engine.h",
    ],
    repository = "@envoy",
    deps = [
        ":engine_common_lib",
        ":engine_types_lib",
        "//library/common/bridge:utility_lib",
        "//library/common/event:provisional_dispatcher_lib",
        "//library/common/http:client_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/logger:logger_delegate_lib",
        "//library/common/network:connectivity_manager_lib",
        "//library/common/stats:utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//envoy/server:lifecycle_notifier_interface",
        "@envoy//envoy/stats:stats_interface",
        "@envoy//source/common/common:thread_impl_lib_posix",
        "@envoy//source/common/common:utility_lib",
        "@envoy//source/common/runtime:runtime_lib",
        "@envoy_build_config//:extension_registry",
    ],
)

envoy_cc_library(
    name = "engine_common_lib",
    srcs = ["engine_common.cc"],
    hdrs = ["engine_common.h"],
    repository = "@envoy",
    deps = [
        "//library/common/extensions/cert_validator/platform_bridge:platform_bridge_cc_proto_descriptor",
        "//library/common/extensions/filters/http/local_error:filter_cc_proto_descriptor",
        "//library/common/extensions/filters/http/network_configuration:filter_cc_proto_descriptor",
        "//library/common/extensions/filters/http/platform_bridge:filter_cc_proto_descriptor",
        "//library/common/extensions/filters/http/socket_tag:filter_cc_proto_descriptor",
        "//library/common/extensions/key_value/platform:platform_cc_proto_descriptor",
        "//library/common/extensions/retry/options/network_configuration:predicate_cc_proto_descriptor",
        "@envoy//source/common/common:minimal_logger_lib",
        "@envoy//source/common/common:random_generator_lib",
        "@envoy//source/common/runtime:runtime_lib",
        "@envoy//source/exe:stripped_main_base_lib",
        "@envoy//source/server:null_overload_manager_lib",
    ] + envoy_select_signal_trace(
        ["@envoy//source/common/signal:sigaction_lib"],
        "@envoy",
    ),
)

envoy_cc_library(
    name = "engine_common_lib_stamped",
    repository = "@envoy",
    deps = [
        ":engine_common_lib",
        "@envoy//source/exe:envoy_stripped_main_base_lib",
    ],
)

envoy_cc_library(
    name = "engine_types_lib",
    hdrs = [
        "engine_types.h",
    ],
    repository = "@envoy",
    deps = [
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/strings",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/common:base_logger_lib",
        "@envoy//source/common/http:header_map_lib",
    ],
)
