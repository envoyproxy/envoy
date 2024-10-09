load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "hash_policy_lib",
    srcs = ["hash_policy_impl.cc"],
    hdrs = ["hash_policy_impl.h"],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/udp:hash_policy_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hash_lib",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "udp_proxy_filter_lib",
    srcs = ["udp_proxy_filter.cc"],
    hdrs = ["udp_proxy_filter.h"],
    deps = [
        ":hash_policy_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/event:file_event_interface",
        "//envoy/event:timer_interface",
        "//envoy/http:header_evaluator",
        "//envoy/network:filter_interface",
        "//envoy/network:listener_interface",
        "//envoy/stream_info:uint32_accessor_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:empty_string",
        "//source/common/common:linked_object",
        "//source/common/common:random_generator_lib",
        "//source/common/network:socket_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:utility_lib",
        "//source/common/router:header_parser_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/extensions/filters/udp/udp_proxy/router:router_lib",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":udp_proxy_filter_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/filter:config_discovery_lib",
        "//source/common/formatter:substitution_format_string_lib",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
    ],
)
