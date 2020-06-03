load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "router_interface",
    hdrs = ["router.h"],
    deps = [
        "//include/envoy/tcp:conn_pool_interface",
        "//include/envoy/upstream:load_balancer_interface",
        "//source/common/upstream:load_balancer_lib",
    ],
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router_impl.cc"],
    hdrs = ["router_impl.h"],
    deps = [
        ":router_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:thread_local_cluster_interface",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/rocketmq_proxy:conn_manager_lib",
    ],
)

envoy_cc_library(
    name = "route_matcher",
    srcs = ["route_matcher.cc"],
    hdrs = ["route_matcher.h"],
    deps = [
        ":router_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/server:filter_config_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:matchers_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/router:metadatamatchcriteria_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/rocketmq_proxy:metadata_lib",
        "@envoy_api//envoy/extensions/filters/network/rocketmq_proxy/v3:pkg_cc_proto",
    ],
)
