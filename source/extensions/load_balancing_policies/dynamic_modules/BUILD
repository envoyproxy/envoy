load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "lb_config_lib",
    srcs = ["lb_config.cc"],
    hdrs = ["lb_config.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/http:async_client_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/stats:stats_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:load_balancer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:thread_lib",
        "//source/common/init:target_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules:metric_registry",
    ],
)

envoy_cc_library(
    name = "load_balancer_lib",
    srcs = ["load_balancer.cc"],
    hdrs = ["load_balancer.h"],
    deps = [
        ":lb_config_lib",
        "//envoy/upstream:load_balancer_interface",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "abi_impl",
    srcs = ["abi_impl.cc"],
    deps = [
        ":load_balancer_lib",
        "//source/common/common:thread_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:message_lib",
        "//source/common/stats:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":abi_impl",
        ":lb_config_lib",
        ":load_balancer_lib",
        "//envoy/server:factory_context_interface",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:load_balancer_factory_base_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/load_balancing_policies/dynamic_modules/v3:pkg_cc_proto",
    ],
)
