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
        "//include/envoy/udp:hash_policy_interface_no_include_prefix",
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
        "//include/envoy/event:file_event_interface_no_include_prefix",
        "//include/envoy/event:timer_interface_no_include_prefix",
        "//include/envoy/network:filter_interface_no_include_prefix",
        "//include/envoy/network:listener_interface_no_include_prefix",
        "//include/envoy/upstream:cluster_manager_interface_no_include_prefix",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/network:socket_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:load_balancer_lib",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    category = "envoy.filters.udp_listener",
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":udp_proxy_filter_lib",
        "//include/envoy/registry:registry_no_include_prefix",
        "//include/envoy/server:filter_config_interface_no_include_prefix",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
    ],
)
