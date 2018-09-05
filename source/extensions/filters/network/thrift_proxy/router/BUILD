licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":router_lib",
        "//include/envoy/registry",
        "//source/extensions/filters/network/thrift_proxy/filters:factory_base_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_config_interface",
        "//source/extensions/filters/network/thrift_proxy/filters:well_known_names",
        "@envoy_api//envoy/config/filter/network/thrift_proxy/v2alpha1/router:router_cc",
    ],
)

envoy_cc_library(
    name = "router_interface",
    hdrs = ["router.h"],
    external_deps = ["abseil_optional"],
    deps = [],
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router_impl.cc"],
    hdrs = ["router_impl.h"],
    deps = [
        ":router_interface",
        "//include/envoy/tcp:conn_pool_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:load_balancer_interface",
        "//include/envoy/upstream:thread_local_cluster_interface",
        "//source/common/common:logger_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/thrift_proxy:app_exception_lib",
        "//source/extensions/filters/network/thrift_proxy:conn_manager_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_converter_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_lib",
        "//source/extensions/filters/network/thrift_proxy:transport_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
        "@envoy_api//envoy/config/filter/network/thrift_proxy/v2alpha1:thrift_proxy_cc",
    ],
)
